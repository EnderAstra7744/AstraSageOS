/*
 * AstraSript Programming Language
 * AstraSript Interpreter V1.3
 *
 * Single-file C interpreter
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif


/* =========================================================
   CONSTANTS
   ========================================================= */

#define MAX_LINES       8192
#define MAX_LINE        4096
#define MAX_VARIABLES   1024
#define MAX_FUNCTIONS   256
#define MAX_NAME        256
#define MAX_VALUE       4096
#define MAX_LOOP        1000000
#define MAX_CALL_DEPTH  100


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


/* =========================================================
   GLOBAL STATE
   ========================================================= */

static Variable variables[MAX_VARIABLES];
static int variable_count = 0;

static Function functions[MAX_FUNCTIONS];
static int function_count = 0;

static bool has_final_end = false;
static bool has_inputc = false;

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


static void astra_error(int line, const char *message)
{
    if (line > 0)
        printf("[AstraSript Error] Line %d: %s\n", line, message);
    else
        printf("[AstraSript Error] %s\n", message);
}


/* =========================================================
   PROGRAM LOADING
   ========================================================= */

static void free_program(Program *program)
{
    if (!program || !program->lines)
        return;

    for (int i = 0; i < program->count; i++)
        free(program->lines[i]);

    free(program->lines);

    program->lines = NULL;
    program->count = 0;
}


static bool load_program(const char *filename, Program *program)
{
    FILE *file;

    program->lines = NULL;
    program->count = 0;

    file = fopen(filename, "r");

    if (!file)
    {
        astra_error(0, "Could not open file.");
        return false;
    }

    program->lines = malloc(sizeof(char *) * MAX_LINES);

    if (!program->lines)
    {
        fclose(file);
        astra_error(0, "Memory allocation failed.");
        return false;
    }

    char buffer[MAX_LINE];
    bool first_line = true;

    while (fgets(buffer, sizeof(buffer), file))
    {
        char *text = buffer;
        size_t length;

        /* Skip UTF-8 BOM */
        if (first_line)
        {
            first_line = false;

            if ((unsigned char)text[0] == 0xEF &&
                (unsigned char)text[1] == 0xBB &&
                (unsigned char)text[2] == 0xBF)
            {
                text += 3;
            }
        }

        length = strlen(text);

        if (length == sizeof(buffer) - 1 &&
            text[length - 1] != '\n' &&
            !feof(file))
        {
            astra_error(program->count + 1, "Line is too long.");
            fclose(file);
            free_program(program);
            return false;
        }

        if (program->count >= MAX_LINES)
        {
            astra_error(0, "Too many lines.");
            fclose(file);
            free_program(program);
            return false;
        }

        program->lines[program->count] = malloc(length + 1);

        if (!program->lines[program->count])
        {
            astra_error(0, "Memory allocation failed.");
            fclose(file);
            free_program(program);
            return false;
        }

        memcpy(program->lines[program->count], text, length + 1);

        program->count++;
    }

    fclose(file);

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

        if (op != '*' && op != '/')
            break;

        parser->position++;

        double right = parse_factor(parser);

        if (op == '*')
        {
            value *= right;
        }
        else
        {
            if (right == 0.0)
            {
                parser->error = true;
                return 0.0;
            }

            value /= right;
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


static void print_number(double value)
{
    if (value > -9e15 && value < 9e15 &&
        value == (double)(long long)value)
    {
        printf("%lld", (long long)value);
    }
    else
    {
        printf("%g", value);
    }
}


static void print_value(Variable *variable)
{
    if (!variable)
        return;

    switch (variable->type)
    {
        case TYPE_STRING:
            printf("%s", variable->string_value);
            break;

        case TYPE_INT:
            printf("%d", variable->int_value);
            break;

        case TYPE_FLOAT:
            printf("%g", variable->float_value);
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
        printf("%s", value);

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
                printf(" ");

                argument_start = cursor + 1;
            }
        }

        cursor++;
    }

    if (*argument_start)
        print_argument(argument_start, line_number);

    printf("\n");
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

    has_inputc = true;

    fflush(stdout);

    char input[MAX_VALUE];

    if (!fgets(input, sizeof(input), stdin))
        return;

    input[strcspn(input, "\r\n")] = '\0';

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

        if (strcmp(line, "end;") == 0 && depth > 0)
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
   TERMINAL COMMANDS
   ========================================================= */

static bool execute_terminal_command(char *line)
{
    /*
     * Placeholder for AstraSript terminal commands.
     * Can be expanded later.
     */

    (void)line;

    return false;
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

        if (!executed)
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
            strcmp(line, "start;") == 0)
        {
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

        if (execute_terminal_command(line))
        {
            i++;
            continue;
        }

        astra_error(i + 1, "Unknown syntax.");

        i++;
    }
}


/* =========================================================
   PROGRAM VALIDATION
   ========================================================= */

static bool validate_program(Program *program)
{
    has_final_end = false;
    has_inputc = false;

    if (program->count <= 0)
    {
        astra_error(0, "Empty AstraSript file.");
        return false;
    }

    /* Check final end; */

    for (int i = program->count - 1; i >= 0; i--)
    {
        char copy[MAX_LINE];

        strncpy(copy, program->lines[i], MAX_LINE - 1);
        copy[MAX_LINE - 1] = '\0';

        char *line = trim(copy);

        if (is_blank(line))
            continue;

        if (strcmp(line, "end;") == 0)
            has_final_end = true;

        break;
    }

    /* Check inputc */

    for (int i = 0; i < program->count; i++)
    {
        char copy[MAX_LINE];

        strncpy(copy, program->lines[i], MAX_LINE - 1);
        copy[MAX_LINE - 1] = '\0';

        char *line = trim(copy);

        if (is_keyword(line, "inputc"))
            has_inputc = true;
    }

    return true;
}


/* =========================================================
   KEEP ALIVE
   ========================================================= */

static void keep_alive(void)
{
    fflush(stdout);

    while (true)
    {
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }
}


/* =========================================================
   RESET STATE
   ========================================================= */

static void reset_state(void)
{
    memset(variables, 0, sizeof(variables));
    memset(functions, 0, sizeof(functions));

    variable_count = 0;
    function_count = 0;
    call_depth = 0;

    has_final_end = false;
    has_inputc = false;
}


/* =========================================================
   RUN FILE
   ========================================================= */

static bool has_as_extension(const char *filename)
{
    size_t length = strlen(filename);

    return length > 3 &&
           filename[length - 3] == '.' &&
           tolower((unsigned char)filename[length - 2]) == 'a' &&
           tolower((unsigned char)filename[length - 1]) == 's';
}


static int run_file(const char *filename)
{
    if (!has_as_extension(filename))
    {
        astra_error(0, "File must have .as extension.");
        return 1;
    }

    Program program;

    if (!load_program(filename, &program))
        return 1;

    reset_state();

    if (!validate_program(&program))
    {
        free_program(&program);
        return 1;
    }

    scan_functions(&program);

    execute_range(&program, 0, program.count);

    free_program(&program);

    fflush(stdout);

    /*
     * If end; is missing, the interpreter never stops.
     * If inputc() exists, the interpreter never stops.
     */

    if (!has_final_end || has_inputc)
        keep_alive();

    return 0;
}


/* =========================================================
   CREDITS
   ========================================================= */

static void show_credits(void)
{
    printf(
        "\n"
        "AstraSript Programming Language\n"
        "Developed by EnderAstra\n"
        "\n"
    );
}


/* =========================================================
   INTERACTIVE MODE
   ========================================================= */

static void interactive_mode(void)
{
    printf("AstraSript Interpreter V1.3\n");
    printf("'Credits' for All Developers for AstraSript\n");
    printf("'Exit' or 'Quit' to exit Interpreter\n");
    printf("'astrasript <FILE NAME.as>' to run your AstraSript file\n");
    printf("\n");

    char input[MAX_VALUE];

    while (true)
    {
        printf("\033[92m\xCE\xA3>>>\033[0m ");
        fflush(stdout);

        if (!fgets(input, sizeof(input), stdin))
            break;

        input[strcspn(input, "\r\n")] = '\0';

        char *command = trim(input);

        if (*command == '\0')
            continue;

        /* Exit */

        if (strcmp(command, "Exit") == 0 ||
            strcmp(command, "exit") == 0 ||
            strcmp(command, "EXIT") == 0 ||
            strcmp(command, "Quit") == 0 ||
            strcmp(command, "quit") == 0 ||
            strcmp(command, "QUIT") == 0)
        {
            break;
        }

        /* Credits */

        if (strcmp(command, "Credits") == 0 ||
            strcmp(command, "credits") == 0 ||
            strcmp(command, "CREDITS") == 0)
        {
            show_credits();
            continue;
        }

        /* astrasript <file> */

        if (is_keyword(command, "astrasript"))
        {
            char *filename = trim(command + 10);

            if (*filename == '\0')
            {
                astra_error(0, "File name missing.");
                continue;
            }

            /* run_file() may intentionally enter keep_alive(). */

            run_file(filename);

            continue;
        }

        printf("[AstraSript Error] Unknown command: %s\n", command);
    }
}


/* =========================================================
   MAIN
   ========================================================= */

int main(int argc, char **argv)
{
    /* No arguments: interactive interpreter */

    if (argc == 1)
    {
        interactive_mode();
        return 0;
    }

    /* Exactly one file argument: astrasript program.as */

    if (argc == 2)
        return run_file(argv[1]);

    astra_error(0, "Invalid arguments.");

    return 1;
}
