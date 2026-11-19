/*
 * AstraScript interpreter - built into as_kernel (AstraSageOS).
 *
 * Ported from the standalone AstraSript Interpreter V1.3. It no longer needs a
 * C library: source comes from the S: drive, output goes to the AST console,
 * and AstraSageOS commands ("as files", "as newfile x", ...) can be written
 * directly inside .as programs.
 */

#include <stdbool.h>
#include "astra.h"
#include "../AstraScript/include/astrascript.h"

#define INT_MAX 2147483647
#define INT_MIN (-2147483647 - 1)

/* freestanding replacements for the libc calls the interpreter uses */
#define strlen   kstrlen
#define strcmp   kstrcmp
#define strncmp  kstrncmp
#define strchr   kstrchr
#define strrchr  kstrrchr
#define strstr   kstrstr
#define strcspn  kstrcspn
#define strncpy  kstrncpy
#define strcpy   kstrcpy
#define memcpy   kmemcpy
#define memset   kmemset
#define snprintf ksnprintf
#define isspace  as_isspace
#define isalpha  as_isalpha
#define isdigit  as_isdigit
#define isalnum  as_isalnum
#define atof     as_atof
#define atoi     as_atoi

static int as_isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
static int as_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int as_isdigit(int c) { return c >= '0' && c <= '9'; }
static int as_isalnum(int c) { return as_isalpha(c) || as_isdigit(c); }

static int as_atoi(const char *s)
{
    int sign = 1, v = 0;
    while (as_isspace((unsigned char)*s)) s++;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    while (as_isdigit((unsigned char)*s) && v < 214748364) v = v * 10 + (*s++ - '0');
    return sign * v;
}

static double as_atof(const char *s)
{
    double sign = 1.0, v = 0.0, scale = 1.0;
    while (as_isspace((unsigned char)*s)) s++;
    if (*s == '-') { sign = -1.0; s++; } else if (*s == '+') s++;
    while (as_isdigit((unsigned char)*s)) v = v * 10.0 + (*s++ - '0');
    if (*s == '.') {
        s++;
        while (as_isdigit((unsigned char)*s)) { v = v * 10.0 + (*s++ - '0'); scale *= 10.0; }
    }
    return sign * v / scale;
}


/* =========================================================
   CONSTANTS
   ========================================================= */

#define MAX_LINES       4096
#define ASC_SRC_MAX    49152      /* program + the packages it pulls in with 'use' */
#define MAX_LINE        256
#define MAX_VARIABLES   96
#define MAX_FUNCTIONS   32
#define MAX_NAME        32
#define MAX_VALUE       256
#define MAX_LOOP        1000000
#define MAX_CALL_DEPTH  16


/* =========================================================
   TYPES
   ========================================================= */

typedef enum
{
    TYPE_STRING,
    TYPE_INT,
    TYPE_FLOAT
} VariableType;


typedef struct
{
    char name[MAX_NAME];

    VariableType type;

    int int_value;
    double float_value;

    char string_value[MAX_VALUE];

} Variable;


typedef struct
{
    char name[MAX_NAME];

    int start_line;
    int end_line;

} Function;


typedef struct
{
    char **lines;
    int count;

} Program;


typedef enum
{
    CMP_EQ,
    CMP_NE,
    CMP_GE,
    CMP_LE,
    CMP_GT,
    CMP_LT
} CompareOp;


static int error_count = 0;

/* =========================================================
   GLOBAL STATE
   ========================================================= */

static Variable variables[MAX_VARIABLES];
static int variable_count = 0;

static Function functions[MAX_FUNCTIONS];
static int function_count = 0;


static int call_depth = 0;


/* =========================================================
   BASIC HELPERS
   ========================================================= */

static char *trim(char *str)
{
    char *end;

    while (isspace((unsigned char)*str))
        str++;

    if (*str == '\0')
        return str;

    end = str + strlen(str) - 1;

    while (end > str && isspace((unsigned char)*end))
        end--;

    end[1] = '\0';

    return str;
}


static bool is_blank(const char *str)
{
    if (!str)
        return true;

    while (*str)
    {
        if (!isspace((unsigned char)*str))
            return false;

        str++;
    }

    return true;
}


static bool starts_with(const char *str, const char *prefix)
{
    return strncmp(str, prefix, strlen(prefix)) == 0;
}


static bool is_ident_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}


/*
 * Keyword match with a word boundary:
 * "else" matches "else {" but NOT "elsewhere = 5".
 */
static bool is_keyword(const char *str, const char *keyword)
{
    size_t length = strlen(keyword);

    return strncmp(str, keyword, length) == 0 &&
           !is_ident_char(str[length]);
}


static bool is_valid_name(const char *name)
{
    if (!(isalpha((unsigned char)*name) || *name == '_'))
        return false;

    for (const char *p = name; *p; p++)
    {
        if (!is_ident_char(*p))
            return false;
    }

    return true;
}


/* First occurrence of c that is not inside a "string". */
static char *find_unquoted(char *text, char c)
{
    bool inside = false;

    for (; *text; text++)
    {
        if (*text == '\\' && inside && text[1])
        {
            text++;
            continue;
        }

        if (*text == '"')
            inside = !inside;
        else if (!inside && *text == c)
            return text;
    }

    return NULL;
}


/* Trims and removes one trailing ';'. */
static char *strip_trailing_semicolon(char *text)
{
    text = trim(text);

    size_t length = strlen(text);

    if (length > 0 && text[length - 1] == ';')
    {
        text[length - 1] = '\0';
        text = trim(text);
    }

    return text;
}


/*
 * Check mode (asc_check): the editor runs the program through the same code
 * paths, but nothing is printed, nothing is read and loops/branches are walked
 * once. Errors are collected as (line, message) instead of being printed.
 */
static int       check_mode;
static asc_diag *diag_out;
static int       diag_max, diag_n;

static void astra_error(int line, const char *message)
{
    error_count++;

    if (check_mode)
    {
        if (line < 1)
            line = 1;

        for (int i = 0; i < diag_n; i++)         /* one message per line is enough */
            if (diag_out[i].line == line)
                return;

        if (diag_n < diag_max)
        {
            diag_out[diag_n].line = line;
            strncpy(diag_out[diag_n].msg, message, sizeof(diag_out[0].msg) - 1);
            diag_out[diag_n].msg[sizeof(diag_out[0].msg) - 1] = '\0';
            diag_n++;
        }

        return;
    }

    if (line > 0)
        kprintf("\033[91m[AstraSript Error] Line %d: %s\033[0m\n", line, message);
    else
        kprintf("\033[91m[AstraSript Error] %s\033[0m\n", message);
}

/* Output wrappers: silent in check mode. */
static void chk_puts(const char *text) { if (!check_mode) kputs(text); }
static void chk_putc(char c)           { if (!check_mode) con_putc(c); }

static void chk_printf(const char *fmt, ...)
{
    char buffer[128];
    va_list ap;

    if (check_mode)
        return;

    va_start(ap, fmt);
    kvsnprintf(buffer, sizeof buffer, fmt, ap);
    va_end(ap);
    kputs(buffer);
}

#define kputs    chk_puts
#define con_putc chk_putc
#define kprintf  chk_printf


static char  source_copy[ASC_SRC_MAX + 2];
static char *line_ptrs[MAX_LINES];


/* =========================================================
   PROGRAM LOADING
   ========================================================= */

/* Splits the source text into NUL-terminated lines (in a private copy). */
static bool load_program(const char *source, u32 length, Program *program)
{
    program->lines = line_ptrs;
    program->count = 0;

    if (length > ASC_SRC_MAX)
    {
        astra_error(0, "Script too large.");
        return false;
    }

    memcpy(source_copy, source, length);
    source_copy[length] = '\0';

    char *text = source_copy;

    /* Skip UTF-8 BOM */
    if ((unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
    {
        text += 3;
    }

    while (*text)
    {
        if (program->count >= MAX_LINES)
        {
            astra_error(0, "Too many lines.");
            return false;
        }

        char *end = text;

        while (*end && *end != '\n')
            end++;

        if (end - text >= MAX_LINE - 1)
        {
            astra_error(program->count + 1, "Line is too long.");
            return false;
        }

        bool last = (*end == '\0');

        *end = '\0';

        program->lines[program->count++] = text;

        if (last)
            break;

        text = end + 1;
    }

    return true;
}


/* =========================================================
   VARIABLES
   ========================================================= */

static Variable *find_variable(const char *name)
{
    for (int i = 0; i < variable_count; i++)
    {
        if (strcmp(variables[i].name, name) == 0)
            return &variables[i];
    }

    return NULL;
}


static Variable *create_variable(const char *name, VariableType type)
{
    Variable *existing = find_variable(name);

    if (existing)
    {
        existing->type = type;
        existing->int_value = 0;
        existing->float_value = 0.0;
        existing->string_value[0] = '\0';

        return existing;
    }

    if (variable_count >= MAX_VARIABLES)
        return NULL;

    Variable *variable = &variables[variable_count++];

    memset(variable, 0, sizeof(Variable));

    strncpy(variable->name, name, MAX_NAME - 1);
    variable->name[MAX_NAME - 1] = '\0';

    variable->type = type;

    return variable;
}


/* =========================================================
   EXPRESSION PARSER
   ========================================================= */

typedef struct
{
    const char *text;
    int position;
    bool error;

} ExprParser;


static void expr_skip_spaces(ExprParser *parser)
{
    while (isspace((unsigned char)parser->text[parser->position]))
        parser->position++;
}


static double parse_expression(ExprParser *parser);


static double parse_factor(ExprParser *parser)
{
    expr_skip_spaces(parser);

    char current = parser->text[parser->position];

    /* Parentheses */

    if (current == '(')
    {
        parser->position++;

        double value = parse_expression(parser);

        expr_skip_spaces(parser);

        if (parser->text[parser->position] == ')')
            parser->position++;
        else
            parser->error = true;

        return value;
    }

    /* Unary minus / plus */

    if (current == '-')
    {
        parser->position++;
        return -parse_factor(parser);
    }

    if (current == '+')
    {
        parser->position++;
        return parse_factor(parser);
    }

    /* Number */

    if (isdigit((unsigned char)current) || current == '.')
    {
        char number[128];
        int index = 0;

        while ((isdigit((unsigned char)parser->text[parser->position]) ||
                parser->text[parser->position] == '.') &&
               index < (int)sizeof(number) - 1)
        {
            number[index++] = parser->text[parser->position++];
        }

        number[index] = '\0';

        return atof(number);
    }

    /* Variable name */

    if (isalpha((unsigned char)current) || current == '_')
    {
        char name[MAX_NAME];
        int index = 0;

        while (is_ident_char(parser->text[parser->position]) &&
               index < MAX_NAME - 1)
        {
            name[index++] = parser->text[parser->position++];
        }

        name[index] = '\0';

        Variable *variable = find_variable(name);

        if (!variable)
        {
            parser->error = true;
            return 0.0;
        }

        if (variable->type == TYPE_INT)
            return variable->int_value;

        if (variable->type == TYPE_FLOAT)
            return variable->float_value;

        parser->error = true;
        return 0.0;
    }

    parser->error = true;

    return 0.0;
}


static double parse_term(ExprParser *parser)
{
    double value = parse_factor(parser);

    while (!parser->error)
    {
        expr_skip_spaces(parser);

        char op = parser->text[parser->position];

        if (op != '*' && op != '/' && op != '%')
            break;

        parser->position++;

        double right = parse_factor(parser);

        if (op == '*')
        {
            value *= right;
        }
        else if (op == '%')                       /* integer remainder */
        {
            long a = (long)value, b = (long)right;

            if (b == 0)
            {
                if (!check_mode)
                {
                    parser->error = true;
                    return 0.0;
                }
            }
            else
            {
                value = (double)(a % b);
            }
        }
        else
        {
            if (right == 0.0)
            {
                if (!check_mode)
                {
                    parser->error = true;
                    return 0.0;
                }
            }
            else
            {
                value /= right;
            }
        }
    }

    return value;
}


static double parse_expression(ExprParser *parser)
{
    double value = parse_term(parser);

    while (!parser->error)
    {
        expr_skip_spaces(parser);

        char op = parser->text[parser->position];

        if (op != '+' && op != '-')
            break;

        parser->position++;

        double right = parse_term(parser);

        if (op == '+')
            value += right;
        else
            value -= right;
    }

    return value;
}


static bool evaluate_expression(const char *expression, double *result)
{
    ExprParser parser;

    parser.text = expression;
    parser.position = 0;
    parser.error = false;

    *result = parse_expression(&parser);

    expr_skip_spaces(&parser);

    if (parser.error || parser.text[parser.position] != '\0')
        return false;

    return true;
}


/* =========================================================
   VALUE HELPERS
   ========================================================= */

/*
 * True only if the whole text is ONE string literal,
 * e.g. "abc" but NOT "a" == "b".
 */
static bool is_quoted_string(const char *text)
{
    size_t length = strlen(text);

    if (length < 2 || text[0] != '"' || text[length - 1] != '"')
        return false;

    for (size_t i = 1; i < length - 1; i++)
    {
        if (text[i] == '\\' && i + 1 < length - 1)
        {
            i++;
            continue;
        }

        if (text[i] == '"')
            return false;
    }

    return true;
}


/*
 * Removes the quotes and processes escapes
 * (\n \t \r \\ \") for quoted strings.
 */
static void unquote(const char *source, char *destination, size_t size)
{
    bool quoted = is_quoted_string(source);

    const char *p = source;
    const char *stop = source + strlen(source);
    size_t out = 0;

    if (quoted)
    {
        p++;
        stop--;
    }

    while (p < stop && out + 1 < size)
    {
        if (quoted && *p == '\\' && p + 1 < stop)
        {
            p++;

            switch (*p)
            {
                case 'n':  destination[out++] = '\n'; break;
                case 't':  destination[out++] = '\t'; break;
                case 'r':  destination[out++] = '\r'; break;
                case '\\': destination[out++] = '\\'; break;
                case '"':  destination[out++] = '"';  break;
                default:
                    destination[out++] = '\\';

                    if (out + 1 < size)
                        destination[out++] = *p;

                    break;
            }

            p++;
            continue;
        }

        destination[out++] = *p++;
    }

    destination[out] = '\0';
}


static void out_double(double v)
{
    /* printf("%g") with 6 significant digits */
    char out[48];
    int o = 0;

    if (v != v) { kputs("nan"); return; }

    if (v < 0) { out[o++] = '-'; v = -v; }

    if (v > 1e300) { out[o] = '\0'; kputs(out); kputs("inf"); return; }

    if (v == 0.0) { kputs(o ? "-0" : "0"); return; }

    int e = 0;
    double a = v;

    while (a >= 10.0 && e < 330) { a /= 10.0; e++; }
    while (a < 1.0 && e > -330)  { a *= 10.0; e--; }

    unsigned digits = (unsigned)(a * 100000.0 + 0.5);

    if (digits >= 1000000u) { digits = 100000u; e++; }

    char d[8];

    for (int i = 5; i >= 0; i--) { d[i] = (char)('0' + digits % 10u); digits /= 10u; }

    int n = 6;

    while (n > 1 && d[n - 1] == '0') n--;

    if (e < -4 || e >= 6)
    {
        out[o++] = d[0];

        if (n > 1)
        {
            out[o++] = '.';

            for (int i = 1; i < n; i++) out[o++] = d[i];
        }

        out[o++] = 'e';
        out[o++] = e < 0 ? '-' : '+';

        int ae = e < 0 ? -e : e;

        if (ae >= 100) out[o++] = (char)('0' + ae / 100);

        out[o++] = (char)('0' + (ae / 10) % 10);
        out[o++] = (char)('0' + ae % 10);
    }
    else if (e >= 0)
    {
        for (int i = 0; i <= e; i++) out[o++] = i < n ? d[i] : '0';

        if (n > e + 1)
        {
            out[o++] = '.';

            for (int i = e + 1; i < n; i++) out[o++] = d[i];
        }
    }
    else
    {
        out[o++] = '0';
        out[o++] = '.';

        for (int i = 0; i < -e - 1; i++) out[o++] = '0';
        for (int i = 0; i < n; i++)      out[o++] = d[i];
    }

    out[o] = '\0';
    kputs(out);
}


static void print_number(double value)
{
    if (value > -9e15 && value < 9e15 &&
        value == (double)(long long)value)
    {
        if (value > -2147483647.0 && value < 2147483647.0)
        {
            kprintf("%d", (int)value);
        }
        else
        {
            /* split into two 32-bit halves: no 64-bit division needed */
            bool negative = value < 0;
            double v = negative ? -value : value;

            int high = (int)(v / 1e9);
            int low  = (int)(v - (double)high * 1e9);

            if (low < 0)           { high--; low += 1000000000; }
            if (low >= 1000000000) { high++; low -= 1000000000; }

            kprintf("%s%d%09d", negative ? "-" : "", high, low);
        }
    }
    else
    {
        out_double(value);
    }
}


static void print_value(Variable *variable)
{
    if (!variable)
        return;

    switch (variable->type)
    {
        case TYPE_STRING:
            kputs(variable->string_value);
            break;

        case TYPE_INT:
            kprintf("%d", variable->int_value);
            break;

        case TYPE_FLOAT:
            out_double(variable->float_value);
            break;
    }
}


/*
 * Assigns a value text to a variable
 * (shared by declarations and assignments).
 */
static void assign_value(Variable *variable, char *value, int line_number)
{
    value = strip_trailing_semicolon(value);

    if (variable->type == TYPE_STRING)
    {
        if (is_quoted_string(value))
        {
            unquote(value, variable->string_value,
                    sizeof(variable->string_value));
        }
        else
        {
            Variable *source = find_variable(value);

            if (source && source->type == TYPE_STRING)
            {
                char temp[MAX_VALUE];

                strcpy(temp, source->string_value);
                strcpy(variable->string_value, temp);
            }
            else
            {
                astra_error(line_number,
                            "String value must be in quotes.");
            }
        }

        return;
    }

    double result;

    if (!evaluate_expression(value, &result))
    {
        astra_error(line_number,
                    "Invalid expression (or division by zero).");
        return;
    }

    if (variable->type == TYPE_INT)
    {
        if (result >= (double)INT_MAX)
            variable->int_value = INT_MAX;
        else if (result <= (double)INT_MIN)
            variable->int_value = INT_MIN;
        else
            variable->int_value = (int)result;
    }
    else
    {
        variable->float_value = result;
    }
}


/* =========================================================
   PRINTC
   ========================================================= */

static void print_argument(char *argument, int line_number)
{
    argument = trim(argument);

    if (*argument == '\0')
        return;

    /* String */

    if (is_quoted_string(argument))
    {
        char value[MAX_VALUE];

        unquote(argument, value, sizeof(value));
        kputs(value);

        return;
    }

    /* Variable */

    Variable *variable = find_variable(argument);

    if (variable)
    {
        print_value(variable);
        return;
    }

    /* Number / expression */

    double result;

    if (evaluate_expression(argument, &result))
    {
        print_number(result);
        return;
    }

    astra_error(line_number, "Unknown variable or invalid expression in printc().");
}


static void execute_printc(char *line, int line_number)
{
    char *start = strchr(line, '(');
    char *end = strrchr(line, ')');

    if (!start || !end || end < start)
    {
        astra_error(line_number, "Invalid printc syntax.");
        return;
    }

    *end = '\0';

    char arguments[MAX_VALUE];

    strncpy(arguments, start + 1, sizeof(arguments) - 1);
    arguments[sizeof(arguments) - 1] = '\0';

    char *cursor = arguments;
    char *argument_start = cursor;

    bool inside_string = false;
    int depth = 0;

    while (*cursor)
    {
        if (*cursor == '\\' && inside_string && cursor[1])
        {
            cursor += 2;
            continue;
        }

        if (*cursor == '"')
        {
            inside_string = !inside_string;
        }
        else if (!inside_string)
        {
            if (*cursor == '(')
            {
                depth++;
            }
            else if (*cursor == ')')
            {
                depth--;
            }
            else if (*cursor == ',' && depth == 0)
            {
                *cursor = '\0';

                print_argument(argument_start, line_number);
                con_putc(' ');

                argument_start = cursor + 1;
            }
        }

        cursor++;
    }

    if (*argument_start)
        print_argument(argument_start, line_number);

    con_putc('\n');
}


/* =========================================================
   INPUTC
   ========================================================= */

static void execute_inputc(char *line, int line_number)
{
    char *start = strchr(line, '(');
    char *end = strrchr(line, ')');

    if (!start || !end || end < start)
    {
        astra_error(line_number, "Invalid inputc syntax.");
        return;
    }

    *end = '\0';

    char *name = trim(start + 1);

    Variable *variable = find_variable(name);

    if (!variable)
    {
        astra_error(line_number, "inputc() variable not found.");
        return;
    }

    if (check_mode)
        return;

    char input[MAX_VALUE];

    ast_input(input, sizeof(input));

    if (variable->type == TYPE_STRING)
    {
        strncpy(variable->string_value, input, MAX_VALUE - 1);
        variable->string_value[MAX_VALUE - 1] = '\0';
    }
    else if (variable->type == TYPE_INT)
    {
        variable->int_value = atoi(input);
    }
    else
    {
        variable->float_value = atof(input);
    }
}


/* =========================================================
   VARIABLE DECLARATIONS
   ========================================================= */

static bool parse_variable_declaration(char *line, int line_number)
{
    if (!is_keyword(line, "vr"))
        return false;

    char copy[MAX_LINE];

    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *cursor = trim(copy + 2);

    /* Type is mandatory */

    char type_name[MAX_NAME];
    int type_index = 0;

    while (*cursor && !isspace((unsigned char)*cursor))
    {
        if (type_index < MAX_NAME - 1)
            type_name[type_index++] = *cursor;

        cursor++;
    }

    type_name[type_index] = '\0';

    VariableType type;

    if (strcmp(type_name, "inter") == 0)
    {
        type = TYPE_INT;
    }
    else if (strcmp(type_name, "flo") == 0)
    {
        type = TYPE_FLOAT;
    }
    else if (strcmp(type_name, "strt") == 0)
    {
        type = TYPE_STRING;
    }
    else
    {
        astra_error(line_number, "Unknown variable type.");
        return true;
    }

    cursor = trim(cursor);

    if (*cursor == '\0')
    {
        astra_error(line_number, "Variable name missing.");
        return true;
    }

    char *equals = strchr(cursor, '=');

    char name[MAX_NAME];

    if (equals)
    {
        size_t name_length = (size_t)(equals - cursor);

        if (name_length >= sizeof(name))
            name_length = sizeof(name) - 1;

        memcpy(name, cursor, name_length);
        name[name_length] = '\0';
    }
    else
    {
        strncpy(name, cursor, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }

    /* "vr inter x;" -> name must not contain ';' */

    char *clean_name = strip_trailing_semicolon(name);

    if (!is_valid_name(clean_name))
    {
        astra_error(line_number, "Invalid variable name.");
        return true;
    }

    Variable *variable = create_variable(clean_name, type);

    if (!variable)
    {
        astra_error(line_number, "Too many variables.");
        return true;
    }

    if (!equals)
        return true;

    assign_value(variable, equals + 1, line_number);

    return true;
}


/* =========================================================
   ASSIGNMENT
   ========================================================= */

/* identifier  =  ...   (but not ==) */
static bool is_assignment_line(const char *line)
{
    const char *p = line;

    if (!(isalpha((unsigned char)*p) || *p == '_'))
        return false;

    while (is_ident_char(*p))
        p++;

    while (isspace((unsigned char)*p))
        p++;

    return *p == '=' && p[1] != '=';
}


static void execute_assignment(char *line, int line_number)
{
    char *equals = strchr(line, '=');

    if (!equals)
        return;

    char name[MAX_NAME];

    size_t name_length = (size_t)(equals - line);

    if (name_length >= sizeof(name))
        name_length = sizeof(name) - 1;

    memcpy(name, line, name_length);
    name[name_length] = '\0';

    Variable *variable = find_variable(trim(name));

    if (!variable)
    {
        astra_error(line_number, "Variable not found.");
        return;
    }

    char expression[MAX_VALUE];

    strncpy(expression, equals + 1, sizeof(expression) - 1);
    expression[sizeof(expression) - 1] = '\0';

    assign_value(variable, expression, line_number);
}


/* =========================================================
   CONDITIONS
   ========================================================= */

static bool resolve_string(char *text, char *out, size_t size)
{
    text = trim(text);

    if (is_quoted_string(text))
    {
        unquote(text, out, size);
        return true;
    }

    Variable *variable = find_variable(text);

    if (variable && variable->type == TYPE_STRING)
    {
        strncpy(out, variable->string_value, size - 1);
        out[size - 1] = '\0';
        return true;
    }

    return false;
}


/* Finds "||" or "&&" outside strings and parentheses. */
static char *find_logical(char *text, const char *op)
{
    size_t length = strlen(op);
    int depth = 0;
    bool inside = false;

    for (char *p = text; *p; p++)
    {
        if (*p == '\\' && inside && p[1])
        {
            p++;
            continue;
        }

        if (*p == '"')
        {
            inside = !inside;
            continue;
        }

        if (inside)
            continue;

        if (*p == '(')
            depth++;
        else if (*p == ')')
            depth--;
        else if (depth == 0 && strncmp(p, op, length) == 0)
            return p;
    }

    return NULL;
}


/* Finds ==, !=, >=, <=, >, < outside strings and parentheses. */
static char *find_comparison(char *text, CompareOp *op, int *length)
{
    int depth = 0;
    bool inside = false;

    for (char *p = text; *p; p++)
    {
        if (*p == '\\' && inside && p[1])
        {
            p++;
            continue;
        }

        if (*p == '"')
        {
            inside = !inside;
            continue;
        }

        if (inside)
            continue;

        if (*p == '(')
        {
            depth++;
        }
        else if (*p == ')')
        {
            depth--;
        }
        else if (depth == 0)
        {
            if (p[0] == '=' && p[1] == '=') { *op = CMP_EQ; *length = 2; return p; }
            if (p[0] == '!' && p[1] == '=') { *op = CMP_NE; *length = 2; return p; }
            if (p[0] == '>' && p[1] == '=') { *op = CMP_GE; *length = 2; return p; }
            if (p[0] == '<' && p[1] == '=') { *op = CMP_LE; *length = 2; return p; }
            if (p[0] == '>')                { *op = CMP_GT; *length = 1; return p; }
            if (p[0] == '<')                { *op = CMP_LT; *length = 1; return p; }
        }
    }

    return NULL;
}


/* "( ... )" where the first '(' closes at the very last character. */
static bool is_wrapped_in_parens(const char *text)
{
    if (text[0] != '(')
        return false;

    int depth = 0;
    bool inside = false;

    for (const char *p = text; *p; p++)
    {
        if (*p == '\\' && inside && p[1])
        {
            p++;
            continue;
        }

        if (*p == '"')
        {
            inside = !inside;
            continue;
        }

        if (inside)
            continue;

        if (*p == '(')
        {
            depth++;
        }
        else if (*p == ')')
        {
            depth--;

            if (depth == 0)
                return p[1] == '\0';
        }
    }

    return false;
}


static bool evaluate_condition_text(char *text, int line_number)
{
    text = trim(text);

    /* a || b */

    char *logical = find_logical(text, "||");

    if (logical)
    {
        *logical = '\0';

        if (evaluate_condition_text(text, line_number))
            return true;

        return evaluate_condition_text(logical + 2, line_number);
    }

    /* a && b */

    logical = find_logical(text, "&&");

    if (logical)
    {
        *logical = '\0';

        if (!evaluate_condition_text(text, line_number))
            return false;

        return evaluate_condition_text(logical + 2, line_number);
    }

    /* !a */

    if (text[0] == '!' && text[1] != '=')
        return !evaluate_condition_text(text + 1, line_number);

    /* ( a ) */

    if (is_wrapped_in_parens(text))
    {
        text[strlen(text) - 1] = '\0';
        return evaluate_condition_text(text + 1, line_number);
    }

    /* Boolean literals */

    if (strcmp(text, "true") == 0 || strcmp(text, "TRUE") == 0)
        return true;

    if (strcmp(text, "false") == 0 || strcmp(text, "FALSE") == 0)
        return false;

    /* Comparison */

    CompareOp op = CMP_EQ;
    int op_length = 0;

    char *position = find_comparison(text, &op, &op_length);

    if (!position)
    {
        astra_error(line_number, "Invalid condition.");
        return false;
    }

    char left[MAX_VALUE];
    char right[MAX_VALUE];

    size_t left_length = (size_t)(position - text);

    if (left_length >= sizeof(left))
        left_length = sizeof(left) - 1;

    memcpy(left, text, left_length);
    left[left_length] = '\0';

    strncpy(right, position + op_length, sizeof(right) - 1);
    right[sizeof(right) - 1] = '\0';

    char *left_text = trim(left);
    char *right_text = trim(right);

    /* String comparison */

    char left_string[MAX_VALUE];
    char right_string[MAX_VALUE];

    bool left_is_string =
        resolve_string(left_text, left_string, sizeof(left_string));

    bool right_is_string =
        resolve_string(right_text, right_string, sizeof(right_string));

    if (left_is_string || right_is_string)
    {
        if (!left_is_string || !right_is_string)
        {
            astra_error(line_number, "Invalid string condition.");
            return false;
        }

        int comparison = strcmp(left_string, right_string);

        switch (op)
        {
            case CMP_EQ: return comparison == 0;
            case CMP_NE: return comparison != 0;
            case CMP_GE: return comparison >= 0;
            case CMP_LE: return comparison <= 0;
            case CMP_GT: return comparison > 0;
            case CMP_LT: return comparison < 0;
        }

        return false;
    }

    /* Numeric comparison */

    double left_value;
    double right_value;

    if (!evaluate_expression(left_text, &left_value) ||
        !evaluate_expression(right_text, &right_value))
    {
        astra_error(line_number, "Invalid condition values.");
        return false;
    }

    switch (op)
    {
        case CMP_EQ: return left_value == right_value;
        case CMP_NE: return left_value != right_value;
        case CMP_GE: return left_value >= right_value;
        case CMP_LE: return left_value <= right_value;
        case CMP_GT: return left_value > right_value;
        case CMP_LT: return left_value < right_value;
    }

    return false;
}


static bool evaluate_condition(const char *condition, int line_number)
{
    char copy[MAX_VALUE];

    strncpy(copy, condition, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *text = trim(copy);

    /* Remove { if it exists */

    char *brace = find_unquoted(text, '{');

    if (brace)
        *brace = '\0';

    /* Remove trailing ; */

    text = strip_trailing_semicolon(text);

    return evaluate_condition_text(text, line_number);
}


/* =========================================================
   STRICT FILES  (start; ... end;)
   A file whose first line is "start;" follows the strict layout rules:
     - the last line is "end;",
     - every line ends with ';' (block headers with '{', block ends are "};"),
     - every { } block finishes with "end;" before its "};".
   Older files without "start;" keep working exactly as before.
   ========================================================= */

static int main_count;
static bool strict_mode;

static const char *code_line(Program *program, int i, bool *in_comment, char *buf)
{
    strncpy(buf, program->lines[i], MAX_LINE - 1);
    buf[MAX_LINE - 1] = '\0';
    char *line = trim(buf);

    if (*in_comment)
    {
        if (strstr(line, "**@"))
            *in_comment = false;
        return NULL;
    }

    if (starts_with(line, "#import") || starts_with(line, "#from"))
        return NULL;                         /* module lines are not code lines for the layout rules */

    if (line[0] == '@')
    {
        if (starts_with(line, "@**") && !strstr(line + 3, "**@"))
            *in_comment = true;
        return NULL;
    }

    return *line ? line : NULL;
}

static bool detect_strict(Program *program, int count)
{
    bool in_comment = false;
    char buf[MAX_LINE];

    for (int i = 0; i < count; i++)
    {
        const char *line = code_line(program, i, &in_comment, buf);
        if (!line)
            continue;
        return strcmp(line, "start;") == 0;
    }

    return false;
}

static void strict_check(Program *program, int count)
{
    bool in_comment = false;
    char buf[MAX_LINE];
    int last = -1, depth = 0;
    char prev[MAX_LINE] = "";
    int first = 1;

    for (int i = 0; i < count; i++)
    {
        const char *line = code_line(program, i, &in_comment, buf);
        if (!line)
            continue;

        size_t n = strlen(line);
        bool opens = line[n - 1] == '{';
        bool closes = line[0] == '}';

        if (first)
        {
            first = 0;                       /* "start;" itself */
            strcpy(prev, line);
            last = i;
            continue;
        }

        if (closes)
        {
            if (strcmp(prev, "end;") != 0)
                astra_error(i + 1, "A block must finish with 'end;' before its '}'.");
            else if (!opens && strcmp(line, "};") != 0)
                astra_error(i + 1, "Missing ';' - a block ends with '};'.");
            if (depth > 0)
                depth--;
        }
        else if (!opens && line[n - 1] != ';' && strcmp(line, "end") != 0)
        {
            astra_error(i + 1, "Missing ';' at the end of the line.");
        }

        if (opens)
            depth++;

        strcpy(prev, line);
        last = i;
    }

    if (last >= 0 && strcmp(prev, "end;") != 0 && strcmp(prev, "end") != 0)
        astra_error(last + 1, "The file must finish with 'end;'.");
}

/* =========================================================
   BLOCK FINDER
   ========================================================= */

/*
 * Finds the line that closes the block that opens at or after "start".
 *
 *   {  ...  }      or      {  ...  end;
 *
 * Braces inside "strings" and inside comments are ignored.
 */
static int find_block_end(Program *program, int start, int limit)
{
    int depth = 0;
    bool in_comment = false;

    for (int i = start; i < limit; i++)
    {
        char copy[MAX_LINE];

        strncpy(copy, program->lines[i], MAX_LINE - 1);
        copy[MAX_LINE - 1] = '\0';

        char *line = trim(copy);

        if (in_comment)
        {
            if (strstr(line, "**@"))
                in_comment = false;

            continue;
        }

        if (line[0] == '@')
        {
            if (starts_with(line, "@**") && !strstr(line + 3, "**@"))
                in_comment = true;

            continue;
        }

        bool inside = false;

        for (char *p = line; *p; p++)
        {
            if (*p == '\\' && inside && p[1])
            {
                p++;
                continue;
            }

            if (*p == '"')
            {
                inside = !inside;
            }
            else if (!inside)
            {
                if (*p == '{')
                {
                    depth++;
                }
                else if (*p == '}')
                {
                    depth--;

                    if (depth <= 0)
                        return i;
                }
            }
        }

        /* end; closes one block that is currently open */

        if (!(strict_mode && i < main_count) && strcmp(line, "end;") == 0 && depth > 0)
        {
            depth--;

            if (depth == 0)
                return i;
        }
    }

    return -1;
}


/*
 * Finds where the block belonging to the header at line i starts/ends.
 * The '{' may be on the same line or alone on the next line.
 */
static bool locate_block(
    Program *program,
    int i,
    int end,
    const char *what,
    int *block_start,
    int *block_end
)
{
    char message[128];
    char copy[MAX_LINE];

    strncpy(copy, program->lines[i], MAX_LINE - 1);
    copy[MAX_LINE - 1] = '\0';

    if (find_unquoted(copy, '{'))
    {
        *block_start = i;
    }
    else
    {
        int j = i + 1;

        while (j < end && is_blank(program->lines[j]))
            j++;

        bool ok = false;

        if (j < end)
        {
            char next[MAX_LINE];

            strncpy(next, program->lines[j], MAX_LINE - 1);
            next[MAX_LINE - 1] = '\0';

            ok = strcmp(trim(next), "{") == 0;
        }

        if (!ok)
        {
            snprintf(message, sizeof(message), "Invalid %s block.", what);
            astra_error(i + 1, message);
            return false;
        }

        *block_start = j;
    }

    *block_end = find_block_end(program, *block_start, end);

    if (*block_end < 0)
    {
        snprintf(message, sizeof(message), "Unclosed %s block.", what);
        astra_error(i + 1, message);
        return false;
    }

    return true;
}


/*
 * Copies the text after a keyword, cuts it at '{' (and optionally ';')
 * outside strings, and returns it trimmed.
 */
static char *get_header(
    const char *source,
    char *out,
    size_t size,
    bool cut_semicolon
)
{
    strncpy(out, source, size - 1);
    out[size - 1] = '\0';

    char *brace = find_unquoted(out, '{');

    if (brace)
        *brace = '\0';

    if (cut_semicolon)
    {
        char *semicolon = find_unquoted(out, ';');

        if (semicolon)
            *semicolon = '\0';
    }

    return trim(out);
}


/* =========================================================
   FUNCTIONS
   ========================================================= */

static void scan_functions(Program *program)
{
    function_count = 0;

    bool in_comment = false;

    for (int i = 0; i < program->count; i++)
    {
        char copy[MAX_LINE];

        strncpy(copy, program->lines[i], MAX_LINE - 1);
        copy[MAX_LINE - 1] = '\0';

        char *line = trim(copy);

        /* Skip comments */

        if (in_comment)
        {
            if (strstr(line, "**@"))
                in_comment = false;

            continue;
        }

        if (line[0] == '@')
        {
            if (starts_with(line, "@**") && !strstr(line + 3, "**@"))
                in_comment = true;

            continue;
        }

        if (!is_keyword(line, "func"))
            continue;

        char header[MAX_VALUE];

        char *name = get_header(line + 4, header, sizeof(header), true);

        /* "func hello()" -> "hello" */

        char *paren = strchr(name, '(');

        if (paren)
        {
            *paren = '\0';
            name = trim(name);
        }

        if (!is_valid_name(name))
        {
            astra_error(i + 1, "Invalid function name.");
            continue;
        }

        int end = find_block_end(program, i, program->count);

        if (end < 0)
        {
            astra_error(i + 1, "Unclosed function.");
            continue;
        }

        if (function_count >= MAX_FUNCTIONS)
        {
            astra_error(i + 1, "Too many functions.");
            continue;
        }

        Function *function = &functions[function_count++];

        strncpy(function->name, name, MAX_NAME - 1);
        function->name[MAX_NAME - 1] = '\0';

        function->start_line = i + 1;
        function->end_line = end;
    }
}


static Function *find_function(const char *name)
{
    for (int i = 0; i < function_count; i++)
    {
        if (strcmp(functions[i].name, name) == 0)
            return &functions[i];
    }

    return NULL;
}


/* =========================================================
   FORWARD DECLARATIONS
   ========================================================= */

static void execute_range(Program *program, int start, int end);

/* function bodies found while checking (checked after the main program) */
static int pending_start[MAX_FUNCTIONS];
static int pending_end[MAX_FUNCTIONS];
static int pending_count;


/* =========================================================
   FUNCTION CALL
   ========================================================= */

/* identifier  (  ...  ) */
static bool is_call_line(const char *line)
{
    const char *p = line;

    if (!(isalpha((unsigned char)*p) || *p == '_'))
        return false;

    while (is_ident_char(*p))
        p++;

    while (isspace((unsigned char)*p))
        p++;

    return *p == '(' && strchr(p, ')') != NULL;
}


static void execute_function_call(Program *program, char *line, int line_number)
{
    char name[MAX_NAME];

    char *open = strchr(line, '(');

    if (!open)
        return;

    size_t length = (size_t)(open - line);

    if (length >= sizeof(name))
        length = sizeof(name) - 1;

    memcpy(name, line, length);
    name[length] = '\0';

    Function *function = find_function(trim(name));

    if (!function)
    {
        astra_error(line_number, "Function not found.");
        return;
    }

    if (check_mode)
        return;                     /* bodies are checked once, where they are defined */

    if (call_depth >= MAX_CALL_DEPTH)
    {
        astra_error(line_number, "Maximum call depth exceeded.");
        return;
    }

    call_depth++;

    execute_range(program, function->start_line, function->end_line);

    call_depth--;
}



/* =========================================================
   GUI STATEMENTS  (gui_open, gui_rect, gui_text, ... - a window of the graphical desktop)
   ========================================================= */

typedef struct { const char *name; int min, max; } gui_cmd;
static const gui_cmd gui_cmds[] = {
    { "gui_open", 3, 3 }, { "gui_close", 0, 0 }, { "gui_clear", 1, 1 }, { "gui_rect", 5, 5 }, { "gui_pixel", 3, 3 },
    { "gui_text", 4, 5 }, { "gui_pal", 2, 2 }, { "gui_sprite", 4, 11 }, { "gui_show", 0, 0 }, { "gui_wait", 2, 2 },
    { "gui_mouse", 2, 2 },
    /* strings, AST commands and files for programs (FroggyCode and friends) */
    { "str_has", 3, 3 }, { "str_cat", 3, 3 }, { "str_word", 3, 3 }, { "str_rest", 3, 3 }, { "str_len", 2, 2 },
    { "str_add", 2, 2 }, { "str_del", 1, 1 }, { "str_low", 2, 2 }, { "str_num", 2, 2 }, { "str_set", 2, 2 }, { "str_sub", 4, 4 }, { "str_line", 3, 3 }, { "str_is", 3, 3 },
    { "ast_do", 2, 2 }, { "file_put", 2, 2 }, { "file_add", 2, 2 }, { "file_has", 2, 2 }, { "file_read", 2, 2 },
};

static bool is_gui_line(const char *line)
{
    if (!is_call_line(line)) return false;
    size_t n = 0;
    while (is_ident_char(line[n])) n++;
    for (size_t i = 0; i < sizeof gui_cmds / sizeof gui_cmds[0]; i++)
        if (strlen(gui_cmds[i].name) == n && strncmp(line, gui_cmds[i].name, n) == 0) return true;
    return strncmp(line, "gui_", 4) == 0;               /* unknown gui_ names get a clear error */
}

/* splits "a, \"b,c\", (1+2)" at top-level commas; returns the count, -1 when there are too many */
static int gui_split(char *text, char *out[], int max)
{
    int n = 0, depth = 0;
    bool in_str = false;
    char *start = text;

    for (char *p = text;; p++)
    {
        if (*p == '\\' && in_str && p[1]) { p++; continue; }
        if (*p == '"') in_str = !in_str;
        else if (!in_str && *p == '(') depth++;
        else if (!in_str && *p == ')') depth--;

        if ((*p == ',' && !in_str && depth == 0) || *p == '\0')
        {
            if (n >= max) return -1;
            bool end = *p == '\0';
            *p = '\0';
            out[n++] = trim(start);
            if (end) break;
            start = p + 1;
        }
    }

    if (n == 1 && out[0][0] == '\0') return 0;
    return n;
}

static bool gui_num(char *a, int line, int *out)
{
    double v;
    if (!evaluate_expression(a, &v)) { astra_error(line, "Invalid number in the gui_ call."); return false; }
    *out = (int)v;
    return true;
}

static bool gui_color(char *a, int line, unsigned *out)
{
    char t[MAX_VALUE];
    if (resolve_string(a, t, sizeof t) && t[0] == '#')
    {
        unsigned v = 0;
        int n = 0;
        for (const char *c = t + 1; *c; c++, n++)
        {
            int d = *c >= '0' && *c <= '9' ? *c - '0' : *c >= 'a' && *c <= 'f' ? *c - 'a' + 10 : *c >= 'A' && *c <= 'F' ? *c - 'A' + 10 : -1;
            if (d < 0) { astra_error(line, "Colour must look like \"#5BBF5A\"."); return false; }
            v = v * 16 + (unsigned)d;
        }
        if (n != 6) { astra_error(line, "Colour must look like \"#5BBF5A\"."); return false; }
        *out = v;
        return true;
    }
    int n;
    if (!gui_num(a, line, &n)) return false;
    *out = (unsigned)n & 0xFFFFFF;
    return true;
}

static bool gui_var_out(char *name, int line, int value)
{
    Variable *v = find_variable(trim(name));
    if (!v || v->type == TYPE_STRING) { astra_error(line, "gui_ call needs a number variable (inter or flo)."); return false; }
    if (v->type == TYPE_INT) v->int_value = value; else v->float_value = value;
    return true;
}


/* text of an argument: a string literal, a string variable, or a number */
static bool arg_text(char *a, int line, char *out, size_t cap)
{
    if (resolve_string(a, out, cap)) return true;
    double d;
    if (!evaluate_expression(a, &d)) { astra_error(line, "Expected a string or a number."); return false; }
    snprintf(out, cap, "%d", (int)d);
    return true;
}

static Variable *str_dest(char *name, int line)
{
    Variable *v = find_variable(trim(name));
    if (!v || v->type != TYPE_STRING) { astra_error(line, "This call needs a string variable (strt) first."); return NULL; }
    return v;
}

static void str_store(Variable *v, const char *text)
{
    strncpy(v->string_value, text, MAX_VALUE - 1);
    v->string_value[MAX_VALUE - 1] = '\0';
}

static int lower_c(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static const char *find_nocase(const char *hay, const char *needle)
{
    if (!*needle) return hay;
    for (; *hay; hay++)
    {
        size_t i = 0;
        while (needle[i] && hay[i] && lower_c((unsigned char)hay[i]) == lower_c((unsigned char)needle[i])) i++;
        if (!needle[i]) return hay;
    }
    return NULL;
}

/* the start of word n (1-based, words are separated by spaces) */
static const char *word_at(const char *s, int n)
{
    while (*s == ' ') s++;
    for (int i = 1; i < n && *s; i++)
    {
        while (*s && *s != ' ') s++;
        while (*s == ' ') s++;
    }
    return s;
}

static void execute_str_family(const char *name, char *a[], int n, int line_number)
{
    char t[MAX_VALUE], u[MAX_VALUE];
    int v;
    (void)n;

    if (strcmp(name, "str_del") == 0)
    {
        Variable *d = str_dest(a[0], line_number);
        if (d) { size_t l = strlen(d->string_value); if (l) d->string_value[l - 1] = '\0'; }
    }
    else if (strcmp(name, "str_set") == 0)
    {
        Variable *d = str_dest(a[0], line_number);
        if (d && arg_text(a[1], line_number, t, sizeof t)) str_store(d, t);
    }
    else if (strcmp(name, "str_cat") == 0)
    {
        Variable *d = str_dest(a[0], line_number);
        if (!d || !arg_text(a[1], line_number, t, sizeof t) || !arg_text(a[2], line_number, u, sizeof u)) return;
        size_t l = strlen(t);
        if (l >= MAX_VALUE - 1) l = MAX_VALUE - 1;
        strncpy(t + l, u, MAX_VALUE - 1 - l);
        t[MAX_VALUE - 1] = '\0';
        str_store(d, t);
    }
    else if (strcmp(name, "str_add") == 0)
    {
        Variable *d = str_dest(a[0], line_number);
        if (!d || !gui_num(a[1], line_number, &v)) return;
        size_t l = strlen(d->string_value);
        if (v >= 32 && v < 127 && l < MAX_VALUE - 2) { d->string_value[l] = (char)v; d->string_value[l + 1] = '\0'; }
    }
    else if (strcmp(name, "str_low") == 0)
    {
        Variable *d = str_dest(a[0], line_number);
        if (!d || !arg_text(a[1], line_number, t, sizeof t)) return;
        for (char *c = t; *c; c++) *c = (char)lower_c((unsigned char)*c);
        str_store(d, t);
    }
    else if (strcmp(name, "str_word") == 0 || strcmp(name, "str_rest") == 0)
    {
        Variable *d = str_dest(a[0], line_number);
        if (!d || !arg_text(a[1], line_number, t, sizeof t) || !gui_num(a[2], line_number, &v)) return;
        const char *w = word_at(t, v < 1 ? 1 : v);
        size_t l = 0;
        if (strcmp(name, "str_word") == 0) while (w[l] && w[l] != ' ') l++;
        else l = strlen(w);
        memcpy(u, w, l);
        u[l] = '\0';
        str_store(d, u);
    }
    else if (strcmp(name, "str_sub") == 0)                 /* str_sub(dest, text, start, count): start is 1-based */
    {
        int st, cnt;
        Variable *d = str_dest(a[0], line_number);
        if (!d || !arg_text(a[1], line_number, t, sizeof t) || !gui_num(a[2], line_number, &st) || !gui_num(a[3], line_number, &cnt)) return;
        int l = (int)strlen(t);
        if (st < 1) st = 1;
        if (st > l + 1) st = l + 1;
        if (cnt < 0) cnt = 0;
        if (cnt > l - st + 1) cnt = l - st + 1;
        memcpy(u, t + st - 1, (size_t)cnt);
        u[cnt] = '\0';
        str_store(d, u);
    }
    else if (strcmp(name, "str_has") == 0 || strcmp(name, "str_is") == 0)
    {
        /* str_has(res, text, "a|b|c"): res = 1 when the text contains one of the alternatives (ignoring case);
         * str_is: the whole text equals one of them */
        if (!arg_text(a[1], line_number, t, sizeof t) || !arg_text(a[2], line_number, u, sizeof u)) return;
        bool whole = name[4] == 'i';
        int found = 0;
        for (char *p = u; ; )
        {
            char *e = p;
            while (*e && *e != '|') e++;
            char saved = *e;
            *e = '\0';
            if (whole) { size_t i = 0; while (t[i] && p[i] && lower_c((unsigned char)t[i]) == lower_c((unsigned char)p[i])) i++; if (!t[i] && !p[i]) found = 1; }
            else if (*p && find_nocase(t, p)) found = 1;
            *e = saved;
            if (!saved) break;
            p = e + 1;
        }
        gui_var_out(a[0], line_number, found);
    }
    else if (strcmp(name, "str_line") == 0)                /* str_line(dest, text, n): the n-th line (1-based) */
    {
        Variable *d = str_dest(a[0], line_number);
        if (!d || !arg_text(a[1], line_number, t, sizeof t) || !gui_num(a[2], line_number, &v)) return;
        const char *l = t;
        for (int i = 1; i < v && *l; i++) { while (*l && *l != '\n') l++; if (*l) l++; }
        size_t k = 0;
        while (l[k] && l[k] != '\n') k++;
        memcpy(u, l, k);
        u[k] = '\0';
        str_store(d, u);
    }
    else if (strcmp(name, "str_len") == 0)
    {
        if (arg_text(a[1], line_number, t, sizeof t)) gui_var_out(a[0], line_number, (int)strlen(t));
    }
    else if (strcmp(name, "str_num") == 0)
    {
        if (arg_text(a[1], line_number, t, sizeof t)) gui_var_out(a[0], line_number, atoi(t));
    }
    else if (strcmp(name, "ast_do") == 0)
    {
        Variable *d = str_dest(a[0], line_number);
        if (!d || !arg_text(a[1], line_number, t, sizeof t)) return;
        if (ast_capture_command(t, u, sizeof u) < 0) { str_store(d, "(this command cannot be used from a program)"); return; }
        str_store(d, u);
    }
    else if (strcmp(name, "file_put") == 0 || strcmp(name, "file_add") == 0)
    {
        if (!arg_text(a[0], line_number, t, sizeof t) || !arg_text(a[1], line_number, u, sizeof u)) return;
        int r = ast_file_put(t, u, name[5] == 'a');
        if (r < 0) astra_error(line_number, r == -1 ? "Not signed in (as login)." : r == -2 ? "File would exceed 4096 bytes." : "Could not write the file.");
    }
    else if (strcmp(name, "file_has") == 0)
    {
        if (!arg_text(a[1], line_number, t, sizeof t)) return;
        gui_var_out(a[0], line_number, ast_file_read(t, u, sizeof u) >= 0 ? 1 : 0);
    }
    else if (strcmp(name, "file_read") == 0)
    {
        Variable *d = str_dest(a[0], line_number);
        if (!d || !arg_text(a[1], line_number, t, sizeof t)) return;
        if (ast_file_read(t, u, sizeof u) < 0) u[0] = '\0';
        str_store(d, u);
    }
}

static void execute_gui(char *line, int line_number)
{
    char copy[MAX_LINE];
    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    char *text = strip_trailing_semicolon(copy);
    char *open = strchr(text, '(');
    char *close = strrchr(text, ')');

    if (!open || !close || close < open) { astra_error(line_number, "Invalid gui_ call."); return; }

    *close = '\0';
    *open = '\0';
    char *name = trim(text);
    const gui_cmd *cmd = 0;
    for (size_t i = 0; i < sizeof gui_cmds / sizeof gui_cmds[0]; i++)
        if (strcmp(name, gui_cmds[i].name) == 0) cmd = &gui_cmds[i];
    if (!cmd) { astra_error(line_number, "Unknown built-in call (gui_, str_, file_, ast_do)."); return; }

    char *a[12];
    int n = gui_split(open + 1, a, 12);
    if (n < cmd->min || n > cmd->max) { astra_error(line_number, "Wrong number of arguments in the gui_ call."); return; }
    if (check_mode) return;

    int v[5];
    unsigned col;
    char s[MAX_VALUE];

    if (strncmp(name, "gui_", 4) != 0) { execute_str_family(name, a, n, line_number); return; }

    if (strcmp(name, "gui_open") == 0)
    {
        if (!resolve_string(a[0], s, sizeof s)) { astra_error(line_number, "gui_open needs a title in quotes."); return; }
        if (!gui_num(a[1], line_number, &v[0]) || !gui_num(a[2], line_number, &v[1])) return;
        if (!sgui_open(s, v[0], v[1])) astra_error(line_number, "No graphical desktop: gui_ needs the AstraSage desktop (not the text console).");
    }
    else if (strcmp(name, "gui_close") == 0) sgui_close();
    else if (strcmp(name, "gui_show") == 0) sgui_show();
    else if (strcmp(name, "gui_clear") == 0) { if (gui_color(a[0], line_number, &col)) sgui_clear(col); }
    else if (strcmp(name, "gui_rect") == 0)
    {
        for (int i = 0; i < 4; i++) if (!gui_num(a[i], line_number, &v[i])) return;
        if (gui_color(a[4], line_number, &col)) sgui_rect(v[0], v[1], v[2], v[3], col);
    }
    else if (strcmp(name, "gui_pixel") == 0)
    {
        for (int i = 0; i < 2; i++) if (!gui_num(a[i], line_number, &v[i])) return;
        if (gui_color(a[2], line_number, &col)) sgui_pixel(v[0], v[1], col);
    }
    else if (strcmp(name, "gui_pal") == 0)
    {
        if (!gui_num(a[0], line_number, &v[0])) return;
        if (gui_color(a[1], line_number, &col)) sgui_pal(v[0], col);
    }
    else if (strcmp(name, "gui_text") == 0)
    {
        v[2] = 1;
        for (int i = 0; i < 2; i++) if (!gui_num(a[i], line_number, &v[i])) return;
        if (!resolve_string(a[2], s, sizeof s))
        {
            double d;
            if (!evaluate_expression(a[2], &d)) { astra_error(line_number, "gui_text needs a string or a number."); return; }
            snprintf(s, sizeof s, "%d", (int)d);
        }
        if (!gui_color(a[3], line_number, &col)) return;
        if (n == 5 && !gui_num(a[4], line_number, &v[2])) return;
        sgui_text(v[0], v[1], s, col, v[2]);
    }
    else if (strcmp(name, "gui_sprite") == 0)
    {
        for (int i = 0; i < 3; i++) if (!gui_num(a[i], line_number, &v[i])) return;
        for (int r = 3; r < n; r++)
        {
            if (!resolve_string(a[r], s, sizeof s)) { astra_error(line_number, "gui_sprite rows must be strings."); return; }
            sgui_sprite_row(v[0], v[1] + (r - 3) * (v[2] < 0 ? -v[2] : v[2]), v[2], s);
        }
    }
    else if (strcmp(name, "gui_wait") == 0)
    {
        if (!gui_num(a[1], line_number, &v[0])) return;
        gui_var_out(a[0], line_number, sgui_wait(v[0]));
    }
    else if (strcmp(name, "gui_mouse") == 0)
    {
        int x, y;
        sgui_mouse(&x, &y);
        if (gui_var_out(a[0], line_number, x)) gui_var_out(a[1], line_number, y);
    }
}

/* =========================================================
   TERMINAL COMMANDS
   ========================================================= */

static bool execute_terminal_command(char *line, int line_number)
{
    /*
     * AstraSageOS commands can be written directly in a script:
     *
     *     as newfile notes.txt;
     *     as write notes.txt Hello\nWorld;
     *     asscript other.as;      (blocked: scripts cannot nest)
     */

    char command[MAX_LINE];

    strncpy(command, line, sizeof(command) - 1);
    command[sizeof(command) - 1] = '\0';

    char *text = strip_trailing_semicolon(command);

    if (!is_keyword(text, "as") && !is_keyword(text, "asscript"))
        return false;

    if (check_mode)
    {
        const char *why = ast_check_command(text);

        if (why)
            astra_error(line_number, why);

        return true;
    }

    ast_exec(text, 1);

    return true;
}


/* =========================================================
   IF / ELSE IF / ELSE
   ========================================================= */

static void execute_if(Program *program, int *index, int end)
{
    int position = *index;
    bool first = true;
    bool executed = false;

    for (;;)
    {
        char copy[MAX_LINE];

        strncpy(copy, program->lines[position], MAX_LINE - 1);
        copy[MAX_LINE - 1] = '\0';

        char *line = trim(copy);

        char *after;
        bool conditional;

        if (first)
        {
            /* "if" */
            first = false;
            after = line + 2;
            conditional = true;
        }
        else
        {
            /* "else" or "else if" */
            after = trim(line + 4);

            if (is_keyword(after, "if"))
            {
                after += 2;
                conditional = true;
            }
            else
            {
                conditional = false;
            }
        }

        char header[MAX_VALUE];

        char *condition = get_header(after, header, sizeof(header), true);

        int block_start;
        int block_end;

        if (!locate_block(
                program,
                position,
                end,
                conditional ? "if" : "else",
                &block_start,
                &block_end))
        {
            *index = end;
            return;
        }

        if (check_mode)
        {
            if (conditional)
                (void)evaluate_condition(condition, position + 1);

            execute_range(program, block_start + 1, block_end);
        }
        else if (!executed)
        {
            bool take = true;

            if (conditional)
                take = evaluate_condition(condition, position + 1);

            if (take)
            {
                execute_range(program, block_start + 1, block_end);
                executed = true;
            }
        }

        *index = block_end + 1;

        /* A plain else ends the chain. */

        if (!conditional)
            return;

        /* Look for a following else / else if. */

        int next = block_end + 1;

        while (next < end && is_blank(program->lines[next]))
            next++;

        if (next >= end)
            return;

        char next_copy[MAX_LINE];

        strncpy(next_copy, program->lines[next], MAX_LINE - 1);
        next_copy[MAX_LINE - 1] = '\0';

        if (!is_keyword(trim(next_copy), "else"))
            return;

        position = next;
    }
}


/* =========================================================
   WHLE LOOP
   ========================================================= */

static void execute_whle(Program *program, int *index, int end)
{
    int i = *index;

    char copy[MAX_LINE];

    strncpy(copy, program->lines[i], MAX_LINE - 1);
    copy[MAX_LINE - 1] = '\0';

    char *line = trim(copy);

    char header[MAX_VALUE];

    char *condition = get_header(line + 4, header, sizeof(header), true);

    int block_start;
    int block_end;

    if (!locate_block(program, i, end, "whle", &block_start, &block_end))
    {
        *index = end;
        return;
    }

    if (check_mode)
    {
        (void)evaluate_condition(condition, i + 1);
        execute_range(program, block_start + 1, block_end);
        *index = block_end + 1;
        return;
    }

    int safety = 0;

    while (evaluate_condition(condition, i + 1))
    {
        execute_range(program, block_start + 1, block_end);

        safety++;

        if (safety >= MAX_LOOP)
        {
            astra_error(i + 1, "Loop limit exceeded.");
            break;
        }
    }

    *index = block_end + 1;
}


/* =========================================================
   FOR LOOP
   ========================================================= */

static void execute_for(Program *program, int *index, int end)
{
    int i = *index;

    char copy[MAX_LINE];

    strncpy(copy, program->lines[i], MAX_LINE - 1);
    copy[MAX_LINE - 1] = '\0';

    char *line = trim(copy);

    char content[MAX_VALUE];

    /* Only cut at '{' - the ';' separators are needed. */

    char *header = get_header(line + 2, content, sizeof(content), false);

    /*
     * Split:  init ; condition ; increment
     */

    char *separator1 = find_unquoted(header, ';');
    char *separator2 = separator1 ? find_unquoted(separator1 + 1, ';') : NULL;

    if (!separator1 || !separator2)
    {
        astra_error(i + 1, "Invalid fr syntax.");
        *index = end;
        return;
    }

    *separator1 = '\0';
    *separator2 = '\0';

    char *part1 = trim(header);
    char *part2 = trim(separator1 + 1);
    char *part3 = strip_trailing_semicolon(separator2 + 1);

    /* Initialization */

    if (is_keyword(part1, "vr"))
    {
        parse_variable_declaration(part1, i + 1);
    }
    else if (is_assignment_line(part1))
    {
        execute_assignment(part1, i + 1);
    }
    else if (*part1 != '\0')
    {
        astra_error(i + 1, "Invalid fr initialization.");
        *index = end;
        return;
    }

    if (!is_assignment_line(part3))
    {
        astra_error(i + 1, "Invalid fr increment (use: i = i + 1).");
        *index = end;
        return;
    }

    int block_start;
    int block_end;

    if (!locate_block(program, i, end, "fr", &block_start, &block_end))
    {
        *index = end;
        return;
    }

    if (check_mode)
    {
        (void)evaluate_condition(part2, i + 1);
        execute_range(program, block_start + 1, block_end);
        execute_assignment(part3, i + 1);
        *index = block_end + 1;
        return;
    }

    int safety = 0;

    while (evaluate_condition(part2, i + 1))
    {
        execute_range(program, block_start + 1, block_end);

        execute_assignment(part3, i + 1);

        safety++;

        if (safety >= MAX_LOOP)
        {
            astra_error(i + 1, "Loop limit exceeded.");
            break;
        }
    }

    *index = block_end + 1;
}


/* =========================================================
   PACKAGES:  use Name;
   ========================================================= */

static char combined[ASC_SRC_MAX + 2];
static char use_loaded[8][32];
static int  use_nloaded;
static const char *ctx_src;               /* extra definitions appended after the program (package files) */
static int legacy_check(const char *source, u32 length, asc_diag *out, int max);
static u32         ctx_len;

/* Reads the package name of a "use Name;" line. Returns 0 on bad syntax. */
static int parse_use(const char *line, char *name, size_t cap)
{
    const char *p = line + 3;
    while (*p == ' ' || *p == '\t') p++;
    size_t n = 0;
    while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')
    {
        if (n + 1 >= cap) return 0;
        name[n++] = *p++;
    }
    name[n] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    if (n == 0 || *p != ';') return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    return *p == '\0';
}

static void use_statement(char *line, int line_no)
{
    char name[32];

    if (!parse_use(line, name, sizeof name))
    {
        astra_error(line_no, "Use: use PackageName;");
        return;
    }
    if (!aspi_installed(name))
        astra_error(line_no, "Package is not installed (as aspi install).");
}

/* Appends the .as files of every package named by a "use" line in `src`. */
static void add_packages(const char *src, u32 len, int depth)
{
    u32 i = 0;
    while (i < len)
    {
        u32 e = i;
        while (e < len && src[e] != '\n') e++;
        char l[MAX_LINE];
        u32 n = e - i;
        if (n >= sizeof l) n = sizeof l - 1;
        memcpy(l, src + i, n);
        l[n] = '\0';
        char *t = trim(l);
        char name[32];
        if (is_keyword(t, "use") && parse_use(t, name, sizeof name))
        {
            int seen = 0;
            for (int k = 0; k < use_nloaded; k++)
                if (strcmp(use_loaded[k], name) == 0) seen = 1;
            if (!seen && use_nloaded < 8 && depth < 6)
            {
                strcpy(use_loaded[use_nloaded++], name);
                u32 before = (u32)strlen(combined);
                if (before + 2 < ASC_SRC_MAX)
                {
                    combined[before] = '\n';
                    int got = aspi_lib_source(name, combined + before + 1, ASC_SRC_MAX - before - 2);
                    if (got < 0) got = 0;
                    combined[before + 1 + got] = '\0';
                    add_packages(combined + before + 1, (u32)got, depth + 1);
                }
            }
        }
        i = e + 1;
    }
}

/* Builds program + packages into `combined`; returns its length. */
static u32 build_combined(const char *source, u32 length)
{
    use_nloaded = 0;
    if (!ctx_src) aspi_lib_reset();
    if (length > ASC_SRC_MAX) length = ASC_SRC_MAX;
    memcpy(combined, source, length);
    combined[length] = '\0';
    if (ctx_src && ctx_len && length + 2 < ASC_SRC_MAX)
    {
        u32 n = ctx_len;
        if (length + 1 + n > ASC_SRC_MAX) n = ASC_SRC_MAX - length - 1;
        combined[length] = '\n';
        memcpy(combined + length + 1, ctx_src, n);
        combined[length + 1 + n] = '\0';
    }
    add_packages(source, length, 0);
    return (u32)strlen(combined);
}


/* =========================================================
   MAIN EXECUTION ENGINE
   ========================================================= */

static void execute_range(Program *program, int start, int end)
{
    int i = start;

    while (i < end)
    {
        char copy[MAX_LINE];

        strncpy(copy, program->lines[i], MAX_LINE - 1);
        copy[MAX_LINE - 1] = '\0';

        char *line = trim(copy);

        if (is_blank(line))
        {
            i++;
            continue;
        }

        /* Comments */

        if (line[0] == '@')
        {
            /*
             * Multiline comment:
             *
             * @**
             * ...
             * **@
             */

            if (starts_with(line, "@**") && !strstr(line + 3, "**@"))
            {
                i++;

                while (i < end)
                {
                    if (strstr(program->lines[i], "**@"))
                    {
                        i++;
                        break;
                    }

                    i++;
                }

                continue;
            }

            i++;
            continue;
        }

        /* Structural tokens */

        if (strcmp(line, "{") == 0 ||
            strcmp(line, "}") == 0 ||
            strcmp(line, "end;") == 0 ||
            strcmp(line, "};") == 0 ||
            (strict_mode && strcmp(line, "end") == 0) ||
            strcmp(line, "start;") == 0)
        {
            i++;
            continue;
        }

        /* use Package;   (the package's AstraScript files are appended by asc_run/asc_check) */

        if (is_keyword(line, "use"))
        {
            use_statement(line, i + 1);
            i++;
            continue;
        }

        /* Function declaration (registered by scan_functions) */

        if (is_keyword(line, "func"))
        {
            int block_end = find_block_end(program, i, end);

            if (block_end < 0)
            {
                astra_error(i + 1, "Unclosed function.");
                return;
            }

            if (check_mode && pending_count < MAX_FUNCTIONS)
            {
                pending_start[pending_count] = i + 1;
                pending_end[pending_count] = block_end;
                pending_count++;
            }

            i = block_end + 1;
            continue;
        }

        if (is_keyword(line, "if"))
        {
            execute_if(program, &i, end);
            continue;
        }

        if (is_keyword(line, "whle"))
        {
            execute_whle(program, &i, end);
            continue;
        }

        if (is_keyword(line, "fr"))
        {
            execute_for(program, &i, end);
            continue;
        }

        /* A stray else (its if was already consumed otherwise) */

        if (is_keyword(line, "else"))
        {
            astra_error(i + 1, "else without matching if.");

            int block_end = find_block_end(program, i, end);

            if (block_end < 0)
                return;

            i = block_end + 1;
            continue;
        }

        if (is_keyword(line, "vr"))
        {
            parse_variable_declaration(line, i + 1);
            i++;
            continue;
        }

        if (is_keyword(line, "printc") && strchr(line, '('))
        {
            execute_printc(line, i + 1);
            i++;
            continue;
        }

        if (is_keyword(line, "inputc") && strchr(line, '('))
        {
            execute_inputc(line, i + 1);
            i++;
            continue;
        }

        if (is_gui_line(line))
        {
            execute_gui(line, i + 1);
            i++;
            continue;
        }

        /* Assignment (before function calls: x = (1 + 2) * 3;) */

        if (is_assignment_line(line))
        {
            execute_assignment(line, i + 1);
            i++;
            continue;
        }

        /* Function call:  name();  */

        if (is_call_line(line))
        {
            execute_function_call(program, line, i + 1);
            i++;
            continue;
        }

        /* Terminal commands */

        if (execute_terminal_command(line, i + 1))
        {
            i++;
            continue;
        }

        astra_error(i + 1, "Unknown syntax.");

        i++;
    }
}


/* =========================================================
   ENTRY POINT
   ========================================================= */

static void reset_state(void)
{
    memset(variables, 0, sizeof(variables));
    memset(functions, 0, sizeof(functions));

    variable_count = 0;
    function_count = 0;
    call_depth = 0;
}


/*
 * Runs one AstraScript program that is already in memory.
 * Returns 0 if it ran without errors, 1 otherwise. Output goes to the AST
 * console and execution always ends with the program (no keep-alive).
 */
int asc_run_ctx(const char *source, u32 length, const char *ctx, u32 ctxlen)
{
    ctx_src = ctx;
    ctx_len = ctxlen;
    int rc = asc_run(source, length);
    ctx_src = 0;
    ctx_len = 0;
    return rc;
}

int asc_run(const char *source, u32 length)
{
    Program program;

    /* Every AstraScript file must begin with "start;" (and finish with "end;" - checked below): files without it do not run. */
    if (!as2_is_strict(source, length))
    {
        bool blank = true;
        for (u32 i = 0; i < length; i++) if (source[i] != ' ' && source[i] != '\n' && source[i] != '\t' && source[i] != '\r') { blank = false; break; }
        if (blank) { astra_error(0, "Empty AstraSript file."); return 1; }
        astra_error(1, "The file must begin with 'start;' (and finish with 'end;').");
        return 1;
    }

    error_count = 0;

    if (!load_program(source, length, &program))
        return 1;
    main_count = program.count;

    if (program.count <= 0)
    {
        astra_error(0, "Empty AstraSript file.");
        return 1;
    }

    if (main_count > 0)                       /* pull in packages named by 'use' */
    {
        char *copy_src = combined;
        (void)copy_src;
        u32 total = build_combined(source, length);
        if (total > length)
        {
            if (!load_program(combined, total, &program))
                return 1;
        }
    }

    reset_state();
    strict_mode = detect_strict(&program, main_count);
    if (strict_mode)
    {
        strict_check(&program, main_count);
        if (error_count)
        {
            strict_mode = false;
            return 1;
        }
        strict_mode = false;                         /* strict files run on engine 2 (AstraScript/) */
        if (!ctx_src) aspi_lib_reset();
        return as2_run(source, length, ctx_src, ctx_len);
    }
    scan_functions(&program);
    execute_range(&program, 0, main_count);

    if (strict_mode && !error_count)               /* a main() function starts by itself */
    {
        Function *m = find_function("main");
        if (m)
        {
            call_depth = 1;
            execute_range(&program, m->start_line, m->end_line);
            call_depth = 0;
        }
    }

    strict_mode = false;
    return error_count ? 1 : 0;
}


static int legacy_check(const char *source, u32 length, asc_diag *out, int max);

int asc_check(const char *source, u32 length, asc_diag *out, int max)
{
    if (!as2_is_strict(source, length) && length > 0 && max > 0)
    {
        out[0].line = 1; strncpy(out[0].msg, "File must begin with 'start;'", sizeof out[0].msg - 1); out[0].msg[sizeof out[0].msg - 1] = '\0';
        return 1;
    }
    return legacy_check(source, length, out, max);
}

/*
 * Static check for the editor: runs the program in check mode and returns the
 * errors found, at most one per line, sorted by line.
 */
int asc_check_ctx(const char *source, u32 length, const char *ctx, u32 ctxlen, asc_diag *out, int max)
{
    ctx_src = ctx;
    ctx_len = ctxlen;
    int n = asc_check(source, length, out, max);
    ctx_src = 0;
    ctx_len = 0;
    return n;
}

static int legacy_check(const char *source, u32 length, asc_diag *out, int max)
{
    Program program;

    check_mode = 1;
    diag_out = out;
    diag_max = max;
    diag_n = 0;
    error_count = 0;
    pending_count = 0;

    if (load_program(source, length, &program) && program.count > 0)
    {
        main_count = program.count;
        u32 total = build_combined(source, length);
        if (total > length && !load_program(combined, total, &program))
            main_count = program.count;
        reset_state();
        strict_mode = detect_strict(&program, main_count);
        if (strict_mode)
        {
            strict_check(&program, main_count);
            as2_check_ctx(source, length, ctx_src, ctx_len);               /* syntax of engine 2 programs */
            main_count = 0;
        }
        else
        {
        scan_functions(&program);
        execute_range(&program, 0, main_count);
        }

        for (int k = 0; k < pending_count; k++)          /* may grow while running */
        {
            if (pending_start[k] >= main_count) continue;    /* package code is checked at install */
            call_depth = 0;
            execute_range(&program, pending_start[k], pending_end[k]);
        }
    }

    check_mode = 0;
    strict_mode = false;
    pending_count = 0;
    reset_state();

    for (int a = 1; a < diag_n; a++)                     /* sort by line */
    {
        asc_diag t = out[a];
        int b = a - 1;

        while (b >= 0 && out[b].line > t.line)
        {
            out[b + 1] = out[b];
            b--;
        }

        out[b + 1] = t;
    }

    return diag_n;
}


/* ---- host side of engine 2 (see sys/as_host_kernel.c) ---- */
void asc_host_error(int line, const char *msg) { astra_error(line, msg); }
int  asc_host_check_mode(void) { return check_mode; }
int  asc_host_module(const char *name, char *buf, int cap) { return ast_file_read(name, buf, cap); }
