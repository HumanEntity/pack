/* Simple meta data format parser and composer. Useful for simple meta data.
 *
 * Before #including,
 *      #define META_IMPL
 *
 * in the file you want the implementation to reside.
 *
 * To parse a string use meta_parse_string and to compose a string use meta_compose */

#ifndef _META_H
#define _META_H

#include <stdbool.h>

#define META_MAX_OBJ_FIELDS 128
#define META_MAX_OBJ_FIELD_LEN 128
#define META_MAX_STRING_LEN 128
#define META_MAX_ARRAY_LEN 128

#ifndef META_EXTERN
#define META_EXTERN extern
#endif

#ifndef META_STATIC
#define META_STATIC static
#endif

/* All the values available in meta file format
 * No floats because of precision problems.
 * If floats are needed they can be passed
 * as strings or be recomputed after loading
 * be the user. */
typedef enum meta_value_type {
        META_VALUETYPE_INT,
        META_VALUETYPE_STRING,

        META_VALUETYPE_ARRAY,
        /* Like in JSON */
        META_VALUETYPE_OBJ,
        META_VALUETYPE_NULL,

        META_VALUETYPE_NUM
} meta_value_type;

struct meta_value;

typedef struct meta_obj {
        char fields[META_MAX_OBJ_FIELD_LEN][META_MAX_OBJ_FIELDS];
        struct meta_value *field_data[META_MAX_OBJ_FIELDS];

        int present;
} meta_obj;

typedef struct meta_array {
        struct meta_value *items[META_MAX_ARRAY_LEN];
        int present;
} meta_array;

/* Basic value type, where it all starts and ends */
typedef struct meta_value {
        meta_value_type type;

        /* The actual data */
        union {
                int integer;
                char string[META_MAX_STRING_LEN];

                meta_array array;
                meta_obj obj;
        } data;
} meta_value;

#include <stdlib.h>

/* Parse a string into meta_value */
META_EXTERN meta_value meta_parse_string ( const char *string );

/* Convert meta_value into a string */
META_EXTERN void meta_compose ( const meta_value *value, char *dest, size_t dest_len );

/* Free meta_value. Some variants need this to not leak memory */
META_EXTERN void meta_free ( const meta_value *value );

/* Object modification utilities */
META_EXTERN bool meta_get_field ( const meta_value *value, const char *field_name, meta_value *field_value );
META_EXTERN bool meta_set_field ( meta_value *value, const char *field_name, const meta_value *field_value );

/* Array modification utilities */
META_EXTERN bool meta_get_nth ( const meta_value *value, int idx, meta_value *out );
/* idx has to smaller or equal (appending) to number of fields present */
META_EXTERN bool meta_set_nth ( meta_value *value, int idx, meta_value *new_value );

META_EXTERN int meta_array_len ( const meta_value *value );

/* Some constructors */
#define meta_new_obj() ( { ( meta_value ){ .type = META_VALUETYPE_OBJ, .data = { 0 } }; } )
#define meta_new_array() ( { ( meta_value ){ .type = META_VALUETYPE_ARRAY, .data = { 0 } }; } )

#define meta_new_integer( int_data )                                                              \
        ( {                                                                                       \
                ( (meta_value) { .type = META_VALUETYPE_INT, .data = { .integer = int_data } } ); \
        } )
#define meta_new_string( string_data )                                                 \
        ( {                                                                            \
                meta_value string_value = { .type = META_VALUETYPE_STRING };           \
                strncpy( string_value.data.string, string_data, META_MAX_STRING_LEN ); \
                string_value;                                                          \
        } )

#endif /* _META_H */

/* Implementation */
#ifdef META_IMPL

#define META_ASSERT( cond, msg, ... )                                                         \
        do {                                                                                  \
                if ( !( cond ) ) {                                                            \
                        fprintf( stderr, "[%s:%d] " msg, __FILE__, __LINE__, ##__VA_ARGS__ ); \
                        exit( -1 );                                                           \
                }                                                                             \
        } while ( 0 );

/* Allow for opting out malloc */
#ifndef META_MALLOC
#define META_MALLOC( size ) malloc( size )
#endif
#ifndef META_REALLOC /* Just for completeness */
#define META_REALLOC( ptr, new_size ) realloc( ptr, new_size )
#endif
#ifndef META_FREE
#define META_FREE( ptr ) free( ptr )
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

META_STATIC meta_obj _meta_parse_obj ( char **string );
META_STATIC meta_array _meta_parse_array ( char **string );
META_STATIC const char *_meta_parse_string ( char **string );
META_STATIC int _meta_parse_int ( char **string );
META_STATIC void _meta_parse_null ( char **string );

META_STATIC void _meta_skip_comment ( char **string ) {
        META_ASSERT( **string == '#', "Expected '#' got %c\n", **string );

        while ( true ) {
                if ( **string == '\n' || **string == 0 )
                        break;
                ++( *string );
        }
}

META_STATIC void _meta_skip_whitespace ( char **string ) {
        while ( true ) {
                char c = **string;
                switch ( c ) {
                case ' ':
                case '\t':
                case '\n':
                case '\r':
                        ( *string )++;
                        break;
                case '#':
                        printf( "Skiping comment\n" );
                        _meta_skip_comment( string );
                        break;
                default:
                        return;
                        break;
                }
        }
}

META_STATIC bool _meta_is_numeric ( char c ) {
        return c >= '0' && c <= '9';
}

META_STATIC bool _meta_is_alphabetic ( char c ) {
        return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || c == '_';
}

META_STATIC bool _meta_is_alphanumeric ( char c ) {
        return _meta_is_numeric( c ) || _meta_is_alphabetic( c );
}

META_STATIC void _meta_load_ident ( char **const string, char *dest, ssize_t dest_len ) {
        META_ASSERT( _meta_is_alphabetic( **string ), "First character of field name ought to be alphabetic\n" );
        int i = 0;
        while ( _meta_is_alphanumeric( **string ) && i < dest_len - 1 /* For null byte */ ) {
                *( dest++ ) = *( ( *string )++ );
        }
        *dest = 0;
}

META_STATIC meta_value _meta_parse_value ( char **string ) {
        switch ( **string ) {
        case '"': {
                const char *str = _meta_parse_string( string );

                meta_value val = (meta_value) {
                    .type = META_VALUETYPE_STRING,
                };
                int i;

                for ( i = 0; i < META_MAX_STRING_LEN && str[i] != 0; ++i ) {
                        val.data.string[i] = str[i];
                }

                META_FREE( (void *) str );
                return val;
        } break;
        case '(':
                return (meta_value) {
                    .type = META_VALUETYPE_OBJ,
                    .data = { .obj = _meta_parse_obj( string ) },
                };
                break;
        case '[':
                return (meta_value) {
                    .type = META_VALUETYPE_ARRAY,
                    .data = { .array = _meta_parse_array( string ) },
                };
                break;
        case 'n':
                _meta_parse_null( string );
                return (meta_value) { .type = META_VALUETYPE_NULL };
                break;
        default:
                if ( _meta_is_numeric( **string ) ) {
                        return (meta_value) { .type = META_VALUETYPE_INT, .data = { .integer = _meta_parse_int( string ) } };
                }
                META_ASSERT( false, "Unknown character '%c'\n", **string );
                break;
        }
}

META_STATIC const char *_meta_parse_string ( char **string ) {
        META_ASSERT( *( ( *string )++ ) == '"', "Expected '\"' got %c\n", *( *string - 1 ) );

        /* First search for end */
        char *searching_ptr = *string;
        while ( *searching_ptr != '"' && *searching_ptr != 0 ) {
                ++searching_ptr;
        }

        META_ASSERT( *searching_ptr != '0', "Unterminated string literal\n" );

        size_t string_len = searching_ptr - *string;
        char *parsed = (char *) META_MALLOC( sizeof( char ) * string_len );

        strncpy( parsed, *string, string_len );

        *string = searching_ptr + 1;

        return parsed;
}

META_STATIC int _meta_parse_int ( char **string ) {
        char *start = *string;
        META_ASSERT( _meta_is_numeric( *( ( *string )++ ) ), "Expected a numeric char got '%c'\n", *( *string - 1 ) );

        while ( _meta_is_numeric( **string ) ) {
                ++( *string );
        }

        size_t len = *string - start;

        char buffer[len + 1];
        buffer[len] = 0;

        int i;
        for ( i = 0; i < len; ++i ) {
                buffer[i] = start[i];
        }

        return atoi( buffer );
}

META_STATIC void _meta_parse_null ( char **string ) {
        META_ASSERT( *( ( *string )++ ) == 'n', "Expected 'n' got %c", *( *string - 1 ) );
        META_ASSERT( *( ( *string )++ ) == 'u', "Expected 'u' got %c", *( *string - 1 ) );
        META_ASSERT( *( ( *string )++ ) == 'l', "Expected 'l' got %c", *( *string - 1 ) );
        META_ASSERT( *( ( *string )++ ) == 'l', "Expected 'l' got %c", *( *string - 1 ) );
}

META_STATIC meta_obj _meta_parse_obj ( char **string ) {
        /* Null ptr string will be parsed as empty object and warning emitted */
        if ( string == NULL ) {
                return (meta_obj) { .present = 0 };
        }

        meta_obj obj = { 0 };

        _meta_skip_whitespace( string );

        META_ASSERT( *( ( *string )++ ) == '(', "Expected '(' got '%c'\n", *( *string - 1 ) );

        while ( **string != ')' ) {
                _meta_skip_whitespace( string );

                /* Load field name */
                _meta_load_ident( string, obj.fields[obj.present], META_MAX_OBJ_FIELD_LEN );
                _meta_skip_whitespace( string );

                /* Check for colon */
                META_ASSERT( *( ( *string )++ ) == ':', "Expected ':' got %c\n", *( *string - 1 ) );
                _meta_skip_whitespace( string );

                obj.field_data[obj.present] = (meta_value *) META_MALLOC( sizeof( meta_value ) );
                *obj.field_data[obj.present] = _meta_parse_value( string );

                ++obj.present;

                _meta_skip_whitespace( string );
        }

        META_ASSERT( *( ( *string )++ ) == ')', "Expected ')' got '%c'\n", *( *string - 1 ) );

        return obj;
}

META_STATIC meta_array _meta_parse_array ( char **string ) {
        META_ASSERT( *( ( *string )++ ) == '[', "Expected '[' got '%c'\n", *( *string - 1 ) );

        meta_array array = { 0 };

        _meta_skip_whitespace( string );
        while ( **string != ']' ) {
                meta_value val = _meta_parse_value( string );
                array.items[array.present] = META_MALLOC( sizeof( meta_value ) );
                *array.items[array.present++] = val;
                _meta_skip_whitespace( string );
        }

        META_ASSERT( *( ( *string )++ ) == ']', "Expected ']' got '%c'\n", *( *string - 1 ) );
        return array;
}

META_EXTERN meta_value meta_parse_string ( const char *string ) {

        char *str = (char *) string;

        return _meta_parse_value( &str );
}

/* Composing of the meta_value back to string */

META_STATIC char *_meta_compose ( const meta_value *value, char *dest, size_t dest_len );

META_STATIC char *_meta_compose_int ( int integer, char *dest, size_t dest_len ) {
        snprintf( dest, dest_len, "%d", integer );
        return dest + strlen( dest );
}

META_STATIC char *_meta_compose_string ( const char *string, char *dest, size_t dest_len ) {
        snprintf( dest, dest_len, "\"%s\"", string );
        return dest + strlen( dest );
}

META_STATIC char *_meta_compose_obj ( const meta_obj *obj, char *dest, size_t dest_len ) {
        char *start;

        start = dest;

        strncat( dest, "( ", dest_len );
        dest += 2;

        int i;
        for ( i = 0; i < obj->present; ++i ) {
                /* Copy field name */
                const char *field_name = obj->fields[i];

                for ( ; *field_name != 0; ++field_name ) {
                        *( dest++ ) = *field_name;
                }
                /* Don't forget about semicolon */
                *( dest++ ) = ':';

                dest = _meta_compose( obj->field_data[i], dest, dest_len - ( dest - start ) );
                *( dest++ ) = ' ';
        }

        strncat( dest++, ")", dest_len );
        return dest;
}

META_STATIC char *_meta_compose_null ( char *dest, size_t dest_len ) {
        strncpy( dest, "null", dest_len );
        return dest + 4 * sizeof( char );
}

META_STATIC char *_meta_compose_array ( const meta_array *arr, char *dest, size_t dest_len ) {
        char *start = dest;

        /* Begin the array */
        strncat( start, "[ ", dest_len );
        dest += 2;

        int i;
        for ( i = 0; i < arr->present; ++i ) {
                dest = _meta_compose( arr->items[i], dest, dest_len );
                strncat( start, " ", dest_len );
                ++dest;
        }

        /* Close the array */
        strncat( start, "]", dest_len );
        ++dest;

        return dest;
}

META_STATIC char *_meta_compose ( const meta_value *value, char *dest, size_t dest_len ) {
        switch ( value->type ) {
        case META_VALUETYPE_INT:
                return _meta_compose_int( value->data.integer, dest, dest_len );
                break;
        case META_VALUETYPE_STRING:
                return _meta_compose_string( value->data.string, dest, dest_len );
                break;
        case META_VALUETYPE_OBJ:
                return _meta_compose_obj( &value->data.obj, dest, dest_len );
                break;
        case META_VALUETYPE_ARRAY:
                return _meta_compose_array( &value->data.array, dest, dest_len );
                break;
        case META_VALUETYPE_NULL:
                return _meta_compose_null( dest, dest_len );
                break;
        /* Shouldn't be here */
        case META_VALUETYPE_NUM:
                return dest;
                break;
        }
}

META_EXTERN void meta_compose ( const meta_value *value, char *dest, size_t dest_len ) {
        char *end = _meta_compose( value, dest, dest_len );
        *end = 0;
}

META_EXTERN void meta_free ( const meta_value *value ) {
        switch ( value->type ) {
        case META_VALUETYPE_INT:
        case META_VALUETYPE_STRING:
        case META_VALUETYPE_NULL:
        case META_VALUETYPE_NUM: /* Shouldn't be possible */
                break;
        case META_VALUETYPE_OBJ: {
                int i;
                for ( i = 0; i < value->data.obj.present; ++i ) {
                        meta_free( value->data.obj.field_data[i] );
                        META_FREE( value->data.obj.field_data[i] );
                }
        } break;
        case META_VALUETYPE_ARRAY: {
                int i;
                for ( i = 0; i < value->data.array.present; ++i ) {
                        meta_free( value->data.array.items[i] );
                        META_FREE( value->data.array.items[i] );
                }
        } break;
        }
}

META_STATIC bool _meta_is_field_sanitized ( const char *field_name ) {
        META_ASSERT( field_name != NULL, "Null string is not sanitized" );

        if ( !_meta_is_alphabetic( *field_name ) ) {
                return false;
        }

        while ( *field_name != 0 ) {
                if ( !_meta_is_alphanumeric( *field_name++ ) ) {
                        return false;
                }
        }

        return true;
}

META_EXTERN bool meta_get_field ( const meta_value *value, const char *field_name, meta_value *field_value ) {
        META_ASSERT( value->type == META_VALUETYPE_OBJ, "Only objects support field access\n" );
        META_ASSERT( _meta_is_field_sanitized( field_name ), "Field name not sanitized \"%s\"\n", field_name );

        int i;
        for ( i = 0; i < value->data.obj.present; ++i ) {
                if ( strcmp( value->data.obj.fields[i], field_name ) == 0 ) {
                        *field_value = *value->data.obj.field_data[i];
                        return true;
                }
        }

        return false;
}

META_EXTERN bool meta_set_field ( meta_value *value, const char *field_name, const meta_value *new_value ) {
        META_ASSERT( value->type == META_VALUETYPE_OBJ, "Only objects support field access\n" );
        META_ASSERT( _meta_is_field_sanitized( field_name ), "Field name not sanitized \"%s\"\n", field_name );

        int i;
        for ( i = 0; i < value->data.obj.present; ++i ) {
                if ( strncmp( value->data.obj.fields[i], field_name, META_MAX_OBJ_FIELD_LEN ) == 0 ) {
                        *value->data.obj.field_data[i] = *new_value;
                        return true;
                }
        }

        if ( value->data.obj.present + 1 < META_MAX_OBJ_FIELDS ) {
                int new = value->data.obj.present;
                ++value->data.obj.present;
                strncpy( value->data.obj.fields[new], field_name, META_MAX_OBJ_FIELD_LEN );
                value->data.obj.field_data[new] = META_MALLOC( sizeof( meta_value ) );
                *value->data.obj.field_data[new] = *new_value;

                return true;
        }

        return false;
}

META_EXTERN bool meta_get_nth ( const meta_value *value, int idx, meta_value *out ) {
        META_ASSERT( value->type == META_VALUETYPE_ARRAY, "Only arrays support indexing\n" );

        if ( idx >= value->data.array.present ) {
                return false;
        }

        *out = *value->data.array.items[idx];

        return true;
}

META_EXTERN bool meta_set_nth ( meta_value *value, int idx, meta_value *new_val ) {
        META_ASSERT( value->type == META_VALUETYPE_ARRAY, "Only arrays support indexing\n" );

        /* Allow for appending that's why > not >= */
        if ( idx > value->data.array.present ) {
                return false;
        }

        if ( idx == value->data.array.present ) {
                ++value->data.array.present;
                value->data.array.items[idx] = META_MALLOC( sizeof( meta_value ) );
                *value->data.array.items[idx] = *new_val;
                return true;
        }

        *value->data.array.items[idx] = *new_val;

        return false;
}

META_EXTERN int meta_array_len ( const meta_value *value ) {
        META_ASSERT( value->type == META_VALUETYPE_ARRAY, "Only arrays have length\n" );
        return value->data.array.present;
}

#endif
