// Utility for packing images into single texture atlas

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define META_IMPL
#include "meta.h"

#define max( a, b )                         \
        ( {                                 \
                __typeof__( a ) _a = ( a ); \
                __typeof__( b ) _b = ( b ); \
                _a > _b ? _a : _b;          \
        } )

#define min( a, b )                         \
        ( {                                 \
                __typeof__( a ) _a = ( a ); \
                __typeof__( b ) _b = ( b ); \
                _a < _b ? _a : _b;          \
        } )

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define ANSI_RED "\e[0;31m"
#define ANSI_GREEN "\e[0;32m"
#define ANSI_YELLOW "\e[0;33m"
#define ANSI_CYAN "\e[0;36m"
#define ANSI_RESET "\e[0m"

#define ANSI_MOVE_UP "\e[F"
#define ANSI_ERASE_LINE "\e[K"

#define CHANGE() fprintf( stderr, ANSI_MOVE_UP ANSI_ERASE_LINE )

#define LOGT( fmt, ... ) fprintf( stderr, ANSI_CYAN fmt ANSI_RESET, ##__VA_ARGS__ )
#define LOGI( fmt, ... ) fprintf( stderr, fmt, ##__VA_ARGS__ )
#define LOGW( fmt, ... ) \
        fprintf( stderr, ANSI_YELLOW fmt ANSI_RESET, ##__VA_ARGS__ )
#define LOGE( fmt, ... ) fprintf( stderr, ANSI_RED fmt ANSI_RESET, ##__VA_ARGS__ )

const char *usage = "USAGE: pack [OPTIONS] -- [IMAGES]\n"
                    "OPTIONS:\n"
                    "\t-h --help\t displays this help message\n"
                    "\t-i       \t Where to output image\n"
                    "\t-o       \t Where to output metadata\n";

typedef struct vec2 {
        int x, y;
} vec2;

struct edge {
        struct vec2 start, end;
};

enum dir {
        RIGHT,
        LEFT,
        UP,
        DOWN,
};

struct rect {
        int width;
        int height;
};

struct outmost_rect {
        vec2 topleft, bottomright;
};

struct image {
        struct rect size;
        const char *name;
        const stbi_uc *pixels;
};

static char *image_output = NULL;
static char *metadata_output = NULL;

#define MAX_IMAGES ( 128 )

static struct image images[MAX_IMAGES];
static int image_count = 0;

static struct vec2 image_locations[MAX_IMAGES];
static int image_location_count = 0;

static struct outmost_rect outmost = {};

// static int image_widths[MAX_IMAGES];
// static int image_heights[MAX_IMAGES];
// static stbi_uc *image_pixels[MAX_IMAGES];

void display_usage ( void );

vec2 edge_parallel ( struct edge edge ) {
        struct vec2 change = {
            edge.end.x - edge.start.x,
            edge.end.y - edge.start.y,
        };

        change.x = min( max( -1, change.x ), 1 );
        change.y = min( max( -1, change.y ), 1 );

        return change;
}

vec2 edge_perpendicular ( struct edge edge ) {
        vec2 change = edge_parallel( edge );

        int buf = change.x;

        change.x = -change.y;
        change.y = buf;

        return change;
}

enum dir edge_dir ( struct edge edge ) {
        vec2 v = edge_parallel( edge );

        printf( "Parallel %d %d\n", v.x, v.y );

        if ( v.x == 1 ) {
                return RIGHT;
        }
        if ( v.x == -1 ) {
                return LEFT;
        }
        if ( v.y == 1 ) {
                return DOWN;
        }
        if ( v.y == -1 ) {
                return UP;
        }

        perror( "Impossible" );
        return -1;
}

struct outmost_rect new_outmost ( vec2 img_topleft, vec2 img_bottomright ) {
        vec2 new_topleft = {
            min( outmost.topleft.x, min( img_topleft.x, img_bottomright.x ) ),
            min( outmost.topleft.y, min( img_topleft.y, img_bottomright.y ) ) };
        vec2 new_bottomright = {
            max( outmost.bottomright.x, max( img_bottomright.x, img_topleft.x ) ),
            max( outmost.bottomright.y, max( img_bottomright.y, img_topleft.y ) ) };

        return (struct outmost_rect) { .topleft = new_topleft,
                                       .bottomright = new_bottomright };
}

int outmost_score ( struct outmost_rect rect ) {
        vec2 size = (vec2) {
            .x = rect.bottomright.x - rect.topleft.x,
            .y = rect.bottomright.y - rect.topleft.y,
        };

        return size.x * size.y;
}

vec2 topleft ( vec2 c1, vec2 c2 ) {
        return (vec2) { min( c1.x, c2.x ), min( c1.y, c2.y ) };
}

vec2 bottomright ( vec2 c1, vec2 c2 ) {
        return (vec2) { max( c1.x, c2.x ), max( c1.y, c2.y ) };
}

void pack ( struct image img, int max_width ) {
        // First image goes in the origin
        if ( image_location_count == 0 ) {
                LOGT( "Base image\n" );
                image_locations[image_location_count++] = (vec2) { 0, 0 };

                outmost.topleft = (vec2) { 0, 0 };
                outmost.bottomright = (vec2) { img.size.width, img.size.height };

                return;
        }

        // int best_score = INT_MAX;
        // int edge = -1;

        vec2 prev = image_locations[image_location_count - 1];
        vec2 new = (vec2) {
            prev.x + images[image_location_count - 1].size.width,
            prev.y,
        };

        vec2 corner = (vec2) {
            new.x + images[image_location_count].size.width,
            new.y + images[image_location_count].size.height,
        };

        if ( corner.x > max_width ) {
                new = (vec2) {
                    0.0,
                    outmost.bottomright.y,
                };

                corner = (vec2) {
                    new.x + images[image_location_count].size.width,
                    new.y + images[image_location_count].size.height,
                };
        }

        image_locations[image_location_count++] = new;

        outmost = new_outmost( new, corner );

        printf( "Outmost %d %d %d %d\n", outmost.topleft.x, outmost.topleft.y,
                outmost.bottomright.x, outmost.bottomright.y );
}

int main ( int argc, char **argv ) {
        // Process arguments
        {
                bool arg_processing = true;
                for ( int i = 1; i < argc; ++i ) {
                        if ( arg_processing ) {
                                if ( strcmp( "-o", argv[i] ) == 0 ) {
                                        metadata_output = argv[++i];
                                        continue;
                                }
                                if ( strcmp( "-i", argv[i] ) == 0 ) {
                                        image_output = argv[++i];
                                        continue;
                                }

                                if ( strcmp( "-h", argv[i] ) == 0 ) {
                                        display_usage();
                                        return 0;
                                }

                                if ( strcmp( "--", argv[i] ) == 0 ) {
                                        arg_processing = false;
                                        continue;
                                }
                        } else {
                                // Add image to image list
                                images[image_count++].name = argv[i];
                        }
                }
        }

        // Error on no images, we don't pack voids here
        if ( image_count == 0 ) {
                LOGE( "Expected images to pack\n" );
                display_usage();
                return -1;
        }

        // Warn on single image, it's just copying it
        if ( image_count == 1 ) {
                LOGW( "Packing single image is just copying it\n" );
        }

        LOGI( "Packing textures\n" );

        // Load images to memory
        for ( int i = 0; i < image_count; ++i ) {
                LOGT( "Loading %s\n", images[i].name );
                images[i].pixels = stbi_load( images[i].name, &images[i].size.width,
                                              &images[i].size.height, NULL, 4 );
                CHANGE();
                LOGT( "Loaded %s\n", images[i].name );
        }

        // Bubble Sort images based on their size
        for ( int i = 0; i < image_count; ++i ) {
                for ( int j = 0; j < image_count - 1; ++j ) {
                        // // Don't check element against itself
                        // if ( i == j ) {
                        //         continue;
                        // }

                        int current_score =
                            images[j]
                                .size.height;                      // images[j].size.width * images[j].size.height;
                        int next_score = images[j + 1].size.width; // images[j + 1].size.width *
                                                                   // images[j + 1].size.height;

                        // If next image is bigger swap
                        if ( next_score > current_score ) {
                                struct image buf = images[j];
                                images[j] = images[j + 1];
                                images[j + 1] = buf;
                        }
                }
        }

        int max_width = images[0].size.width + images[1].size.width;

        for ( int i = 0; i < image_count; ++i ) {
                pack( images[i], max_width );
        }

        // Print locations
        for ( int i = 0; i < image_location_count; ++i ) {
                printf( "%s X: %d Y: %d W: %d H %d\n", images[i].name, image_locations[i].x,
                        image_locations[i].y, images[i].size.width, images[i].size.height );
        }

        int width = outmost.bottomright.x - outmost.topleft.x;
        int height = outmost.bottomright.y - outmost.topleft.y;

        unsigned char *data = malloc( sizeof( unsigned char ) * width * height * 4 );

        int x_offset = -outmost.topleft.x;
        int y_offset = -outmost.topleft.y;

        for ( int i = 0; i < image_count; ++i ) {
                struct image img = images[i];
                vec2 topleft = image_locations[i];

                printf( "Writing %s at %d %d\n", images[i].name, topleft.x, topleft.y );

                for ( int h = 0; h < img.size.height; ++h ) {
                        for ( int w = 0; w < img.size.width; ++w ) {
                                int pixel_idx = ( h * img.size.width + w ) * 4;
                                int global_pixel_idx =
                                    ( ( topleft.y + h + y_offset ) * width + w + topleft.x + x_offset ) * 4;

                                // printf( "Pixel idx %d\n", pixel_idx );
                                // printf( "GPixel idx %d\n", global_pixel_idx );

                                data[global_pixel_idx] = img.pixels[pixel_idx];
                                data[global_pixel_idx + 1] = img.pixels[pixel_idx + 1];
                                data[global_pixel_idx + 2] = img.pixels[pixel_idx + 2];
                                data[global_pixel_idx + 3] = img.pixels[pixel_idx + 3];
                        }
                }
        }

        LOGI( "Atlas generated\n" );

        stbi_write_png( image_output, width, height, 4, data,
                        sizeof( unsigned char ) * width * 4 );

        // Output the correct metadata

        LOGI( "Generate metadata\n" );

        const int base_len = 1024 * 4;

        meta_value image_data =
            (meta_value) { .type = META_VALUETYPE_OBJ, .data = { .obj = { .present = 0 } } };

        for ( int i = 0; i < image_count; ++i ) {
                float uv_left = (float) image_locations[i].x / (float) width;
                float uv_top = (float) image_locations[i].y / (float) height;

                float uv_right =
                    (float) ( image_locations[i].x + images[i].size.width ) / (float) width;
                float uv_bottom =
                    (float) ( image_locations[i].y + images[i].size.height ) / (float) height;

                meta_value image_desc = (meta_value) { .type = META_VALUETYPE_OBJ,
                                                       .data = { .obj = { .present = 0 } } };

                meta_set_field( &image_desc, "x",
                                &(meta_value) { .type = META_VALUETYPE_INT,
                                                .data = { .integer = image_locations[i].x } } );
                meta_set_field( &image_desc, "y",
                                &(meta_value) { .type = META_VALUETYPE_INT,
                                                .data = { .integer = image_locations[i].y } } );

                meta_set_field( &image_desc, "width",
                                &(meta_value) { .type = META_VALUETYPE_INT,
                                                .data = { .integer = images[i].size.width } } );
                meta_set_field( &image_desc, "height",
                                &(meta_value) { .type = META_VALUETYPE_INT,
                                                .data = { .integer = images[i].size.height } } );

                meta_set_field( &image_data, images[i].name, &image_desc );

                // snprintf( file_metadata, 512,
                // "[%s]\ntop=%d\nleft=%d\nwidth=%d\nheight=%d\nuv_top=\"%f\"\nuv_left=\"%f\"\nuv_bottom=\"%f\"\nuv_right=\"%f\"\n\n",
                // images[i].name, image_locations[i].x, image_locations[i].x,
                // images[i].size.width, images[i].size.height, uv_top, uv_left, uv_bottom,
                // uv_right );
                //
                // strlcat( metadata, file_metadata, base_len );
        }

        // Allocate some space for metadata
        char *metadata = malloc( sizeof( char ) * base_len );
        for ( int i = 0; i < base_len; ++i ) {
                metadata[i] = 0;
        }

        printf( "Compose\n" );
        meta_compose( &image_data, metadata, sizeof( char ) * base_len );
        printf( "Compose2\n" );

        // strlcat( metadata, "# Computer generated do not edit\n\n", base_len );
        //
        // for ( int i = 0; i < image_count; ++i ) {
        //         char *file_metadata = malloc( sizeof( char ) * 512 );
        //
        //         float uv_left = (float) image_locations[i].x / (float) width;
        //         float uv_top = (float) image_locations[i].y / (float) height;
        //
        //         float uv_right = (float) ( image_locations[i].x +
        //         images[i].size.width ) / (float) width; float uv_bottom = (float) (
        //         image_locations[i].y + images[i].size.height ) / (float) height;
        //
        //         snprintf( file_metadata, 512,
        //         "[%s]\ntop=%d\nleft=%d\nwidth=%d\nheight=%d\nuv_top=\"%f\"\nuv_left=\"%f\"\nuv_bottom=\"%f\"\nuv_right=\"%f\"\n\n",
        //         images[i].name, image_locations[i].x, image_locations[i].x,
        //         images[i].size.width, images[i].size.height, uv_top, uv_left,
        //         uv_bottom, uv_right );
        //
        //         strlcat( metadata, file_metadata, base_len );
        // }
        //
        // printf( "%s", metadata );

        FILE *f = fopen( metadata_output, "w" );

        printf( "%s End\n", metadata );
        fputs( metadata, f );

        fclose( f );

        return 0;
}

void display_usage ( void ) { fprintf( stderr, "%s\n", usage ); }
