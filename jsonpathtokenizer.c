#include "jsonpathtokenizer.h"
#include <stdbool.h>

int extract_bracket_contents(char * start, char *buffer);
void tokenize_bracket_contents(char * contents, struct token * tok);
void tokenize_filter_expression(char * contents, struct token * tok);

typedef enum {
    SQUARE_BRACKETS,
    PARENTHESES,
} balanced_brackets_t;

typedef enum {
    DOUBLE_QUOTES,
    SINGLE_QUOTES
} balanced_quotes_t;

bool tokenize(char ** p, struct token * tok) {

    char buffer[250];

    switch(**p) {

        case '$':
            tok->type = ROOT;
            break;
        case '[':
        case '.':

            if(*(*p+1) == '.') {
                (*p)++;
                tok->type = DEEP_SCAN;
                tok->prop.type = SINGLE_KEY;
                tok->prop.index_count = 0;
            } else if(*(*p+1) == '*') {
                (*p)++;
                tok->type = WILD_CARD;
                break;
            } else {
                tok->type = CHILD_KEY;
                tok->prop.type = SINGLE_KEY;
            }

            /*
             * Extract dot-notated or bracket-notated property name
             */
            if(*(*p+1) == '[') {
                (*p)++;
            }

            if(*(*p+1) == '\'') {
                (*p)++;
            }

            strcpy(tok->prop.val, ""); //TODO Needed?

            /*
             * Keep extracting until property selector ends
             */
            while(*(*p+1) != '\0' && *(*p+1) != '.' && *(*p+1) != '[' && *(*p+1) != ']' && *(*p+1) != '\'') {
                (*p)++;
                strncat(tok->prop.val, *p, 1);
            }

            if(*(*p+1) == '\'') {
                (*p)++;
            }

            if(*(*p+1) == ']') {
                (*p)++;
            }

            /*
             * Is next token an array index operator?
             */
            if(*(*p+1) == '[' && *(*p+2 ) != '\'') {
                (*p)++;
            }

            /*
             * Parse array index operator
             */
            if(**p == '[') {

                memset(buffer,0,strlen(buffer));

                if(!extract_balanced_element_contents(*p, buffer, SQUARE_BRACKETS)) {
                    printf("Unable to extract contents");
                }

                (*p) += strlen(buffer) + 1;

                tokenize_bracket_contents(buffer, tok);
            }
            break;
        case '\0':
        default:
            return false;
    }

    (*p)++;

    return true;
}

int extract_balanced_element_contents(char * start, char *buffer, balanced_brackets_t b_type) {

    char open;
    char close;

    switch(b_type) {
        case SQUARE_BRACKETS:
            open = '[';
            close = ']';
            break;
        case PARENTHESES:
            open = '(';
            close = ')';
            break;
    }

    if(*start != open) {
        return 0;
    }

    int x = 0;
    char * p;
    p = start;

    while(*p != '\0') {
        if(*p == open) {
            --x;
        }

        if(*p == close) {
            ++x;
            if(x == 0) {
                start++;
                strncpy(buffer, start, p - start);
                buffer[(p - start)] = '\0';
                return 1;
            }
        }

        ++p;
    }

    return 0;
}

int extract_quoted_contents(char * start, char *buffer, balanced_quotes_t b_type) {

    const char * quote;

    switch(b_type) {
        case DOUBLE_QUOTES:
            quote = "\"";
            break;
        case SINGLE_QUOTES:
            quote = "'";
            break;
    }

    if(*start != *quote) {
        return 0;
    }

    start++;

    char * needle;

    needle = strstr(start, quote);

    if(*needle == *quote) {
        strncpy(buffer, start, needle - start);
        buffer[(needle - start)] = '\0';
        return 1;
    }

    return 0;
}

void tokenize_bracket_contents(char * contents, struct token * tok)
{
    char * what;
    int count = 0;

    what = strpbrk(contents, ":*,?");

    if(what != NULL) {
        switch(*what) {
            case ':':
                what = strtok(contents, ":");
                tok->prop.indexes[0] = atoi(what); //TODO error checking
                what = strtok(NULL, ":");
                tok->prop.indexes[1] = atoi(what); //TODO error checking
                tok->prop.index_count = 2;
                tok->prop.type = RANGE;
                break;
            case '*':
                tok->prop.type = ANY;
                break;
            case ',':
                tok->prop.type = INDEX;

                count = 0;
                what = strtok(contents, ",");
                tok->prop.index_count = 0;

                while(what != NULL) {
                    tok->prop.indexes[count++] = atoi(what); //TODO error checking
                    tok->prop.index_count++;
                    what = strtok(NULL, ",");
                }
                break;
            case '?':
                tok->prop.type = FILTER;
                contents++;
                tokenize_filter_expression(contents, tok);
                break;

        }
    } else {
        tok->prop.index_count = 1;
        tok->prop.indexes[0] = atoi(contents); //TODO error checking
        tok->prop.type = INDEX;
    }
}

void tokenize_filter_expression(char * contents, struct token * tok)
{
    char * p;
    char buffer[250];
    char buffer2[250];
    char * what;

    if(!extract_balanced_element_contents(contents, buffer, PARENTHESES)) {
        printf("Unable to extract contents ()");
    }

    printf("Extracted string: `%s`\n", buffer);

    p = buffer;

    memset(tok,0,strlen(buffer));


    while(*p != '\0') {

        memset(tok,0,strlen(buffer2));

        switch(*p) {

            case '@':
                p++;
                if(*p != '.') {
                    printf("Missing period .");
                }
                p++;

                what = strpbrk(p, " <>)");


                strncpy(buffer, p, what - p);
                buffer[(what - p)] = '\0';
                printf("Extracted a name: %s\n", buffer);

                break;
            case '\'':
                if(!extract_quoted_contents(p, buffer2, SINGLE_QUOTES)) {
                    printf("Unable to extract contents ''");
                } else {
                    p += strlen(buffer2) + 1;
                    printf("Extracted contents from single quotes: %s\n", buffer2);
                }
                break;
            case '"':
                if(!extract_quoted_contents(p, buffer2, DOUBLE_QUOTES)) {
                    printf("Unable to extract contents ''");
                } else {
                    p += strlen(buffer2) + 1;
                    printf("Extracted contents from double quotes: %s\n", buffer2);
                }
                break;
            case '<':
                if(*(p+1) == '=') {
                    printf("Found <=\n");
    //                tok->expr.op = LTE;
                    p++;
                } else {
                    printf("Found <\n");
    //                tok->expr.op = LT;
                }
                break;
            case '>':
                if(*(p+1) == '=') {
                    printf("Found >=\n");
    //                tok->expr.op = GTE;
                    p++;
                } else {
                    printf("Found >\n");
    //                tok->expr.op = GT;
                }
                break;
            case '!':
                break;
            case '=':
                break;
            case ')':
                break;
            case '$':
                break;
            case '/':
                break;
            default:
                break;
        }


        p++;
    }
}