@ color.ash - ANSI colours for the AstraSageOS terminal.
@ Use:  #from <color.ash> import Color      then   printc(Color.RED"Red Text"Color.RESET);
class Color {
    RESET = "\e[0m";
    BOLD = "\e[1m";
    UNDERLINE = "\e[4m";
    BLACK = "\e[30m";
    RED = "\e[31m";
    GREEN = "\e[32m";
    YELLOW = "\e[33m";
    BLUE = "\e[34m";
    MAGENTA = "\e[35m";
    CYAN = "\e[36m";
    WHITE = "\e[37m";
    GRAY = "\e[90m";
    BRIGHT_RED = "\e[91m";
    BRIGHT_GREEN = "\e[92m";
    BRIGHT_YELLOW = "\e[93m";
    BRIGHT_BLUE = "\e[94m";
    BRIGHT_MAGENTA = "\e[95m";
    BRIGHT_CYAN = "\e[96m";
    BRIGHT_WHITE = "\e[97m";
}
