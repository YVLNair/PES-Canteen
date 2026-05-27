// Include the microhttpd library for creating HTTP servers
// This library provides the foundation for handling web requests and responses
#include <microhttpd.h>

// Include JSON-C library for parsing and creating JSON data structures
// Essential for handling API requests and responses in JSON format
#include <json-c/json.h>

// Include Cairo graphics library for 2D graphics rendering
// This is the main library used for drawing and creating visual content
#include <cairo/cairo.h>

// Include Cairo PDF surface support for generating PDF documents
// Extends Cairo functionality to output directly to PDF format
#include <cairo/cairo-pdf.h>

// Include PNG library for handling PNG image files
// Used for loading and manipulating PNG images like backgrounds
#include <png.h>

// Include standard input/output library for basic I/O operations
// Provides functions like printf, fprintf for console output and file operations
#include <stdio.h>

// Include standard library for memory allocation and general utilities
// Provides malloc, free, and other essential system functions
#include <stdlib.h>

// Include string manipulation library for string operations
// Provides functions like strcmp, strcpy, strlen for text processing
#include <string.h>

// Include time library for date and timestamp functionality
// Used to add current date and time to generated bills
#include <time.h>

// Include pthread library for thread synchronization and safety
// Enables multi-threaded operation with proper locking mechanisms
#include <pthread.h>

// Define macro to enable mathematical constants like M_PI
// Ensures math constants are available across different platforms
#define _USE_MATH_DEFINES

// Include math library for mathematical functions and constants
// Provides trigonometric functions needed for drawing rounded rectangles
#include <math.h>

// Include FreeType library build configuration
// Sets up the foundation for custom font loading and rendering
#include <ft2build.h>

// Include FreeType main header for font manipulation
// Provides core functionality for loading and using custom fonts
#include FT_FREETYPE_H

// Include Cairo-FreeType integration for font rendering
// Bridges FreeType fonts with Cairo's rendering system
#include <cairo/cairo-ft.h>

// Define the port number where the web server will listen
// Port 8888 is used for HTTP requests from client applications
#define PORT 8888

// Define the width of PDF pages in points (8.5 inches)
// Standard US Letter width for consistent document formatting
#define PAGE_WIDTH 612

// Define the height of PDF pages in points (11 inches)
// Standard US Letter height for proper document proportions
#define PAGE_HEIGHT 792

// Define structure to represent individual items in the shopping cart
// Contains name, price, and quantity for each product
typedef struct { char name[100]; float price; int quantity; } Item;

// Define structure for storing binary data in memory buffers
// Used to hold PDF data before sending it as HTTP response
typedef struct { unsigned char *data; size_t size; } MemoryBuffer;

// Declare global array to store shopping cart items
// Acts as the main data store for items before bill generation
Item plate[100];

// Initialize counter for number of items currently in the cart
// Tracks how many items are stored in the plate array
int plate_size = 0;

// Initialize mutex for thread-safe access to the shopping cart
// Prevents data corruption when multiple requests access the cart simultaneously
pthread_mutex_t plate_mutex = PTHREAD_MUTEX_INITIALIZER;

// Define safe string copy function to prevent buffer overflows
// Ensures destination string is always null-terminated and within bounds
void safe_strcpy(char *dest, const char *src, size_t dest_size) { 
    strncpy(dest, src, dest_size - 1); 
    dest[dest_size - 1] = '\0'; 
}

// Function to draw rounded rectangles using Cairo graphics
// Creates smooth corners by drawing arcs at each corner of the rectangle
void draw_rounded_rectangle(cairo_t *cr, double x, double y, double width, double height, double radius) { 
    // Convert degrees to radians for trigonometric calculations
    // Required for Cairo's arc drawing functions
    double degrees = M_PI / 180.0; 
    
    // Start a new path to avoid connecting to previous drawing operations
    // Ensures the rounded rectangle is drawn as an independent shape
    cairo_new_sub_path(cr); 
    
    // Draw top-right corner arc from -90 to 0 degrees
    // Creates the curved corner at the upper right of the rectangle
    cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees); 
    
    // Draw bottom-right corner arc from 0 to 90 degrees
    // Creates the curved corner at the lower right of the rectangle
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees); 
    
    // Draw bottom-left corner arc from 90 to 180 degrees
    // Creates the curved corner at the lower left of the rectangle
    cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees); 
    
    // Draw top-left corner arc from 180 to 270 degrees
    // Creates the curved corner at the upper left of the rectangle
    cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees); 
    
    // Close the path to complete the rounded rectangle shape
    // Connects the last point back to the starting point
    cairo_close_path(cr); 
}

// Function to add items to the shopping cart with thread safety
// Returns different codes: 0=new item, 1=updated existing, -1=full, -2=invalid
int add_to_plate(const char *name, float price, int quantity) { 
    // Lock the mutex to ensure thread-safe access to shared data
    // Prevents race conditions when multiple requests modify the cart
    pthread_mutex_lock(&plate_mutex); 
    
    // Initialize return value to indicate failure by default
    // Will be updated based on the operation's success
    int ret = -1; 
    
    // Validate input parameters to ensure they're positive values
    // Prevents adding items with invalid price or quantity
    if (price <= 0 || quantity <= 0) { 
        pthread_mutex_unlock(&plate_mutex); 
        return -2; 
    } 
    
    // Search through existing items to check for duplicates
    // If found, update quantity instead of adding a new entry
    for (int i = 0; i < plate_size; i++) { 
        if (strcmp(plate[i].name, name) == 0) { 
            // Add to existing quantity instead of creating duplicate entry
            // More efficient and prevents duplicate items in the bill
            plate[i].quantity += quantity; 
            ret = 1; 
            goto done; 
        } 
    } 
    
    // Check if there's space for a new item in the cart
    // Prevents buffer overflow by limiting to 100 items maximum
    if (plate_size < 100) { 
        // Safely copy the item name to prevent buffer overflows
        // Uses custom safe_strcpy function for security
        safe_strcpy(plate[plate_size].name, name, sizeof(plate[plate_size].name)); 
        
        // Store the price for this new item
        // Used for calculating totals and displaying on the bill
        plate[plate_size].price = price; 
        
        // Store the quantity for this new item
        // Tracks how many units of this item were ordered
        plate[plate_size].quantity = quantity; 
        
        // Increment the total count of items in the cart
        // Maintains accurate count for array bounds checking
        plate_size++; 
        
        // Set return value to indicate successful addition of new item
        // Distinguishes from updating existing item (return value 1)
        ret = 0; 
    } 
    
    // Label for cleanup and mutex unlocking
    // Ensures proper resource management regardless of execution path
    done: 
    pthread_mutex_unlock(&plate_mutex); 
    return ret; 
}

// Function to clear all items from the shopping cart
// Resets the cart to empty state for new orders
void clear_plate() { 
    // Lock mutex to ensure thread-safe modification of shared data
    // Prevents corruption during cart clearing operation
    pthread_mutex_lock(&plate_mutex); 
    
    // Reset the item count to zero, effectively emptying the cart
    // Simple and efficient way to clear all items at once
    plate_size = 0; 
    
    // Unlock mutex to allow other threads to access the cart
    // Releases the lock after completing the clearing operation
    pthread_mutex_unlock(&plate_mutex); 
}

// Function to generate JSON representation of the current bill
// Creates structured data for API responses and client consumption
char* generate_bill_json() { 
    // Lock mutex for thread-safe read access to cart data
    // Ensures consistent data while generating the JSON response
    pthread_mutex_lock(&plate_mutex); 
    
    // Create root JSON object to hold the entire bill structure
    // This will contain items array and total price
    struct json_object *bill = json_object_new_object(); 
    
    // Create JSON array to hold all individual item objects
    // Each item will be a separate object within this array
    struct json_object *items_array = json_object_new_array(); 
    
    // Initialize running total for calculating grand total
    // Accumulates the cost of all items in the cart
    float total_price = 0; 
    
    // Iterate through all items in the cart to process each one
    // Builds JSON objects for each item and calculates totals
    for (int i = 0; i < plate_size; i++) { 
        // Create JSON object for the current item
        // Will hold name, price, quantity, and calculated total
        struct json_object *item = json_object_new_object(); 
        
        // Calculate total cost for this specific item
        // Multiplies unit price by quantity ordered
        float item_total = plate[i].price * plate[i].quantity; 
        
        // Add item name to the JSON object
        // Provides human-readable identification of the product
        json_object_object_add(item, "name", json_object_new_string(plate[i].name)); 
        
        // Add unit price to the JSON object
        // Shows the cost per individual item
        json_object_object_add(item, "price", json_object_new_double(plate[i].price)); 
        
        // Add quantity to the JSON object
        // Shows how many units of this item were ordered
        json_object_object_add(item, "quantity", json_object_new_int(plate[i].quantity)); 
        
        // Add calculated total for this item to the JSON object
        // Shows the total cost for all units of this item
        json_object_object_add(item, "total", json_object_new_double(item_total)); 
        
        // Add this item object to the items array
        // Builds the complete list of all ordered items
        json_object_array_add(items_array, item); 
        
        // Add this item's total to the grand total
        // Accumulates the overall bill amount
        total_price += item_total; 
    } 
    
    // Add the complete items array to the bill object
    // Provides detailed breakdown of all ordered items
    json_object_object_add(bill, "items", items_array); 
    
    // Add the grand total to the bill object
    // Provides the final amount due for the entire order
    json_object_object_add(bill, "total_price", json_object_new_double(total_price)); 
    
    // Convert the JSON object to a formatted string
    // Creates human-readable JSON with proper indentation
    const char *json_str = json_object_to_json_string_ext(bill, JSON_C_TO_STRING_PRETTY); 
    
    // Create a copy of the JSON string that can be safely returned
    // Necessary because the original string is tied to the JSON object
    char *result = strdup(json_str); 
    
    // Free the JSON object and all its nested components
    // Prevents memory leaks by cleaning up allocated JSON structures
    json_object_put(bill); 
    
    // Unlock mutex to allow other threads to access cart data
    // Releases the lock after completing JSON generation
    pthread_mutex_unlock(&plate_mutex); 
    
    // Return the JSON string to the caller
    // Provides the complete bill data in JSON format
    return result; 
}

// Callback function for writing PDF data to memory buffer
// Used by Cairo to stream PDF content into a memory buffer instead of a file
static cairo_status_t write_to_memory_buffer(void *closure, const unsigned char *data, unsigned int length) { 
    // Cast the closure pointer to our MemoryBuffer structure
    // Provides access to the buffer where PDF data will be stored
    MemoryBuffer *buffer = (MemoryBuffer *)closure; 
    
    // Reallocate buffer to accommodate new data chunk
    // Expands the buffer size to fit both existing and new data
    unsigned char *new_data = realloc(buffer->data, buffer->size + length); 
    
    // Check if memory reallocation was successful
    // Returns error status if memory allocation fails
    if (new_data == NULL) return CAIRO_STATUS_WRITE_ERROR; 
    
    // Copy new data chunk to the end of existing buffer
    // Appends the new PDF data to what's already been written
    memcpy(new_data + buffer->size, data, length); 
    
    // Update buffer pointer to the newly allocated memory
    // Ensures the buffer structure points to the correct memory location
    buffer->data = new_data; 
    
    // Update buffer size to reflect the additional data
    // Maintains accurate tracking of total buffer content
    buffer->size += length; 
    
    // Return success status to Cairo
    // Indicates the write operation completed successfully
    return CAIRO_STATUS_SUCCESS; 
}

// Function to generate PDF bill and return it as memory buffer
// Creates a complete PDF document with bill details and returns raw PDF data
unsigned char* generate_pdf_to_memory(size_t *pdf_size) {
    // Initialize memory buffer structure for storing PDF data
    // Will accumulate PDF content as Cairo generates it
    MemoryBuffer buffer = { .data = NULL, .size = 0 };
    
    // Create PDF surface that writes to memory instead of file
    // Uses callback function to stream PDF data into memory buffer
    cairo_surface_t *surface = cairo_pdf_surface_create_for_stream(write_to_memory_buffer, &buffer, PAGE_WIDTH, PAGE_HEIGHT);
    
    // Create Cairo context for drawing operations on the PDF surface
    // This context will be used for all drawing and text rendering
    cairo_t *cr = cairo_create(surface);

    // Initialize FreeType library variables for custom font loading
    // These will handle loading and managing the custom font file
    FT_Library ft_library = NULL;
    FT_Face ft_face = NULL;
    cairo_font_face_t *cairo_font_face = NULL;
    FT_Error ft_error;

    // Initialize the FreeType library for font operations
    // Required before any font loading or manipulation can occur
    ft_error = FT_Init_FreeType(&ft_library);
    if (ft_error) {
        // Print error message if FreeType initialization fails
        // Helps with debugging font-related issues
        fprintf(stderr, "Error: Could not initialize FreeType library.\n");
    } else {
        // Specify path to the custom font file
        // This font will be used for the main title text
        const char* font_path = "fonts/Barriecito-Regular.ttf";
        
        // Load the font face from the specified file
        // Creates a FreeType face object for the custom font
        ft_error = FT_New_Face(ft_library, font_path, 0, &ft_face);
        if (ft_error) {
            // Print detailed error if font loading fails
            // Includes both file path and error code for debugging
            fprintf(stderr, "Error: Failed to load font '%s'. Freetype Error Code: %d\n", font_path, ft_error);
        } else {
            // Set pixel size for the font before creating Cairo font face
            // Critical step that ensures proper font rendering in Cairo
            ft_error = FT_Set_Pixel_Sizes(ft_face, 0, 42);
            if (ft_error) {
                // Print error if font size setting fails
                // Font size is crucial for proper text rendering
                fprintf(stderr, "Error: Failed to set font pixel size. Freetype Error Code: %d\n", ft_error);
            } else {
                // Create Cairo font face from the FreeType face
                // Bridges FreeType font with Cairo's rendering system
                cairo_font_face = cairo_ft_font_face_create_for_ft_face(ft_face, 0);
            }
        }
    }

    // Load background image from PNG file for the PDF
    // Provides visual branding and professional appearance
    cairo_surface_t *image_surface = cairo_image_surface_create_from_png("pdfbg.png");
    if (cairo_surface_status(image_surface) == CAIRO_STATUS_SUCCESS) {
        // Get dimensions of the loaded background image
        // Needed for scaling calculations to fit the page
        double img_w = cairo_image_surface_get_width(image_surface);
        double img_h = cairo_image_surface_get_height(image_surface);

        // Calculate scale factors for both width and height
        // Determines how much to resize the image to fit the page
        double scale_x = PAGE_WIDTH / img_w;
        double scale_y = PAGE_HEIGHT / img_h;

        // Choose the smaller scale to preserve aspect ratio
        // Prevents image distortion while ensuring it fits on the page
        double scale = (scale_x < scale_y) ? scale_x : scale_y;

        // Calculate offsets to center the image on the page
        // Ensures the background image is properly positioned
        double offset_x = (PAGE_WIDTH - img_w * scale) / 2;
        double offset_y = (PAGE_HEIGHT - img_h * scale) / 2;

        // Save current Cairo state before transformations
        // Allows restoration of original coordinate system later
        cairo_save(cr);
        
        // Move coordinate system to center the image
        // Positions the image at the calculated center point
        cairo_translate(cr, offset_x, offset_y);
        
        // Scale the coordinate system to resize the image
        // Applies the calculated scaling to fit the page
        cairo_scale(cr, scale, scale);
        
        // Set the background image as the drawing source
        // Prepares the image for rendering onto the PDF
        cairo_set_source_surface(cr, image_surface, 0, 0);
        
        // Render the background image onto the PDF
        // Actually draws the scaled and positioned background
        cairo_paint(cr);
        
        // Restore the original coordinate system
        // Returns to normal coordinates for subsequent drawing operations
        cairo_restore(cr);
    }
    
    // Clean up the background image surface
    // Frees memory used by the background image
    cairo_surface_destroy(image_surface);

    // Set custom font if it was loaded successfully
    // Applies the custom font for title text rendering
    if (cairo_font_face && cairo_font_face_status(cairo_font_face) == CAIRO_STATUS_SUCCESS) {
        cairo_set_font_face(cr, cairo_font_face);
    }
    
    // Set text color to dark blue for the main title
    // Creates professional appearance with branded color scheme
    cairo_set_source_rgb(cr, 0.0, 0.192, 0.325);
    
    // Set large font size for the main title
    // Makes the business name prominent and easily readable
    cairo_set_font_size(cr, 42);
    
    // Position cursor for title text
    // Centers the title horizontally on the page
    cairo_move_to(cr, 210, 85);
    
    // Draw the business name as the main title
    // Renders "Grab'N'Go" using the custom font and styling
    cairo_show_text(cr, "Grab'N'Go");

    // Get current system time for timestamp
    // Provides accurate date and time for the bill
    time_t t = time(NULL); 
    struct tm *tm_info = localtime(&t); 
    
    // Format timestamp into readable string
    // Creates standard date-time format for display
    char time_str[100];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    // Set smaller font size for header information
    // Makes secondary information readable but not dominant
    cairo_set_font_size(cr, 12);
    
    // Switch to monospace font for consistent spacing
    // Ensures aligned text for date/time and token information
    cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    
    // Position and draw date/time label
    // Places timestamp information in the upper left area
    cairo_move_to(cr, 40, 110);
    cairo_show_text(cr, "Date & Time: ");
    cairo_show_text(cr, time_str);
    
    // Position and draw token number
    // Places order tracking number in the upper right area
    cairo_move_to(cr, 480, 110); 
    cairo_show_text(cr, "Token No: 100");
    
    // Set semi-transparent white color for table background
    // Creates subtle background that doesn't interfere with text readability
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
    
    // Calculate dynamic height based on number of items
    // Ensures table size adjusts to accommodate all ordered items
    const double header_height = 35;
    const double row_height = 25;
    const double footer_height = 35;
    const double total_row_height = 25;
    double rect_height = header_height + (plate_size * row_height) + footer_height + total_row_height;

    // Draw rounded rectangle background for the items table
    // Creates professional-looking container for bill details
    draw_rounded_rectangle(cr, 40, 250, 515, rect_height, 15);
    cairo_fill(cr);
    
    // Set dark color for table text
    // Ensures good contrast against the light background
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    
    // Set bold font for table headers
    // Makes column headers stand out from data rows
    cairo_select_font_face(cr, "Barriecito", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 12);
    
    // Define column positions for consistent table layout
    // Creates aligned columns for serial number, item name, quantity, and price
    const double x_sno = 60, x_item = 130, x_qty = 350, x_price = 450, x_total_label = 420, x_total_value = 475;
    
    // Draw table column headers
    // Creates clear labels for each data column
    cairo_move_to(cr, x_sno, 275);
    cairo_show_text(cr, "S.No");
    cairo_move_to(cr, x_item, 275);
    cairo_show_text(cr, "Item Name");
    cairo_move_to(cr, x_qty, 275);
    cairo_show_text(cr, "Quantity");
    cairo_move_to(cr, x_price, 275);
    cairo_show_text(cr, "Price (₹)");
    
    // Set light gray color for separator line
    // Creates subtle visual separation between header and data
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
    cairo_set_line_width(cr, 0.5);
    
    // Draw horizontal line under table headers
    // Visually separates headers from data rows
    cairo_move_to(cr, 50, 285);
    cairo_line_to(cr, 545, 285);
    cairo_stroke(cr);
    
    // Lock mutex for thread-safe access to cart data
    // Ensures consistent data while rendering the table
    pthread_mutex_lock(&plate_mutex);
    
    // Initialize variables for table row rendering
    // Tracks vertical position and running total
    double y_pos = 305;
    float grand_total = 0;
    
    // Set normal font weight for data rows
    // Makes data rows visually distinct from bold headers
    cairo_select_font_face(cr, "Barriecito", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    
    // Iterate through all items to render table rows
    // Creates a row for each item in the shopping cart
    for (int i = 0; i < plate_size; i++) {
        // Add alternating row background for better readability
        // Creates zebra-striped table for easier data scanning
        if (i % 2 != 0) {
             cairo_set_source_rgba(cr, 0.94, 0.92, 0.98, 0.7);
             cairo_rectangle(cr, 50, y_pos - 13, 495, 20); 
             cairo_fill(cr); 
        }
        
        // Prepare formatted strings for numeric data
        // Converts numbers to strings for display in the table
        char sno_str[10], qty_str[10], price_str[20];
        
        // Add this item's total to the grand total
        // Accumulates the overall bill amount
        grand_total += plate[i].price * plate[i].quantity;
        
        // Format serial number, quantity, and price for display
        // Creates properly formatted strings for table cells
        snprintf(sno_str, sizeof(sno_str), "%d", i + 1);
        snprintf(qty_str, sizeof(qty_str), "%d", plate[i].quantity);
        snprintf(price_str, sizeof(price_str), "₹%.2f", plate[i].price);
        
        // Set text color back to dark for data visibility
        // Ensures data text has good contrast
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        
        // Render serial number in first column
        // Provides sequential numbering for each item
        cairo_move_to(cr, x_sno, y_pos);
        cairo_show_text(cr, sno_str);
        
        // Render item name in second column
        // Shows the product name for identification
        cairo_move_to(cr, x_item, y_pos);
        cairo_show_text(cr, plate[i].name);
        
        // Render quantity in third column
        // Shows how many units were ordered
        cairo_move_to(cr, x_qty, y_pos);
        cairo_show_text(cr, qty_str);
        
        // Render unit price in fourth column
        // Shows the cost per individual item
        cairo_move_to(cr, x_price, y_pos);
        cairo_show_text(cr, price_str);
        
        // Move to next row position
        // Advances vertical position for the next item
        y_pos += 25;
    }
    
    // Add spacing before total line
    // Creates visual separation between items and total
    y_pos += 10;
    
    // Draw separator line above total
    // Visually separates item details from final total
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
    cairo_move_to(cr, 50, y_pos);
    cairo_line_to(cr, 545, y_pos);
    cairo_stroke(cr);
    
    // Set bold font for total amount display
    // Makes the final total prominent and easy to find
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14);
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    
    // Format grand total for display
    // Creates properly formatted currency string
    char total_str[100];
    snprintf(total_str, sizeof(total_str), "₹%.2f", grand_total);
    
    // Render "Total" label
    // Clearly identifies the final amount due
    cairo_move_to(cr, x_total_label, y_pos + 20);
    cairo_show_text(cr, "Total");
    
    // Render grand total amount
    // Shows the final bill amount prominently
    cairo_move_to(cr, x_total_value, y_pos + 20);
    cairo_show_text(cr, total_str);
    
    // Unlock mutex after completing table rendering
    // Releases lock on cart data
    pthread_mutex_unlock(&plate_mutex);

    // Clean up Cairo context
    // Frees resources used for drawing operations
    cairo_destroy(cr);
    
    // Finalize the PDF surface
    // Ensures all content is written to the memory buffer
    cairo_surface_finish(surface);

    // Clean up font resources if they were loaded
    // Prevents memory leaks from font loading operations
    if (cairo_font_face) { cairo_font_face_destroy(cairo_font_face); }
    if (ft_face) { FT_Done_Face(ft_face); }
    if (ft_library) { FT_Done_FreeType(ft_library); }

    // Return PDF size and data to caller
    // Provides both the size and content of the generated PDF
    *pdf_size = buffer.size;
    return buffer.data;
}

// Structure to hold POST data for HTTP requests
// Manages incoming data from client applications
struct connection_info_struct {
    char *post_data;
    size_t post_data_size;
};

// Main HTTP request handler function
// Processes all incoming web requests and generates appropriate responses
static enum MHD_Result answer_to_connection(
    void *cls,
    struct MHD_Connection *connection,
    const char *url,
    const char *method,
    const char *version,
    const char *upload_data,
    size_t *upload_data_size,
    void **con_cls
) {
    // Get connection info structure or create if first call
    // Manages state across multiple calls for the same request
    struct connection_info_struct *con_info = *con_cls;
    struct MHD_Response *response;
    int ret;

    // Initialize connection info on first call
    // Sets up data structure for handling POST data
    if (NULL == con_info) {
        con_info = calloc(1, sizeof(struct connection_info_struct));
        if (NULL == con_info) return MHD_NO;
        *con_cls = con_info;
        return MHD_YES;
    }

    // Handle CORS preflight requests for cross-origin access
    // Enables web browsers to make requests from different domains
    if (strcmp(method, "OPTIONS") == 0) {
        response = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        MHD_add_response_header(response, "Access-Control-Allow-Methods", "POST, GET, OPTIONS");
        MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
        MHD_add_response_header(response, "Access-Control-Max-Age", "86400");
        ret = MHD_queue_response(connection, MHD_HTTP_NO_CONTENT, response);
        MHD_destroy_response(response);
        return ret;
    }

    // Handle incoming POST data by accumulating it in buffer
    // Collects all chunks of data sent by the client
    if (strcmp(method, "POST") == 0 && *upload_data_size > 0) {
        char *new_data = realloc(con_info->post_data, con_info->post_data_size + *upload_data_size + 1);
        if (!new_data) {
            free(con_info->post_data);
            return MHD_NO;
        }
        con_info->post_data = new_data;
        memcpy(con_info->post_data + con_info->post_data_size, upload_data, *upload_data_size);
        con_info->post_data_size += *upload_data_size;
        con_info->post_data[con_info->post_data_size] = '\0';
        *upload_data_size = 0;
        return MHD_YES;
    }

    // Handle "/confirm" endpoint for PDF generation
    // Generates and returns PDF bill when order is confirmed
    if (strcmp(url, "/confirm") == 0 && strcmp(method, "POST") == 0) {
        size_t pdf_size;
        unsigned char *pdf_buffer = generate_pdf_to_memory(&pdf_size);

        // Check if PDF generation was successful
        // Handles errors gracefully with appropriate HTTP status
        if (pdf_buffer == NULL || pdf_size == 0) {
            const char *error_msg = "{\"error\": \"Failed to generate PDF. Check console for error messages.\"}";
            response = MHD_create_response_from_buffer(strlen(error_msg), (void *)error_msg, MHD_RESPMEM_PERSISTENT);
            MHD_add_response_header(response, "Content-Type", "application/json");
            MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
            ret = MHD_queue_response(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, response);
        } else {
            // Send successful PDF response with proper headers
            // Configures browser to download the PDF as an attachment
            response = MHD_create_response_from_buffer(pdf_size, pdf_buffer, MHD_RESPMEM_MUST_FREE);
            MHD_add_response_header(response, "Content-Type", "application/pdf");
            MHD_add_response_header(response, "Content-Disposition", "attachment; filename=\"bill.pdf\"");
            MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
            ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
            
            // Clear cart after successful bill generation
            // Resets the system for the next order
            clear_plate();
        }
    } else {
        // Handle other endpoints ("/add" and "/bill")
        // Processes item additions and bill viewing requests
        char *response_body = NULL;
        enum MHD_ResponseMemoryMode mem_mode = MHD_RESPMEM_PERSISTENT;
        int status_code = MHD_HTTP_OK;

        // Handle "/add" endpoint for adding items to cart
        // Processes JSON data to add new items or update quantities
        if (strcmp(url, "/add") == 0 && strcmp(method, "POST") == 0) {
            if (!con_info->post_data) {
                response_body = "{\"error\": \"No data received\"}";
                status_code = MHD_HTTP_BAD_REQUEST;
            } else {
                // Parse incoming JSON data
                // Extracts item details from client request
                struct json_object *parsed = json_tokener_parse(con_info->post_data);
                if (!parsed) {
                    response_body = "{\"error\": \"Invalid JSON\"}";
                    status_code = MHD_HTTP_BAD_REQUEST;
                } else {
                    // Extract required fields from JSON
                    // Gets name, price, and quantity for the new item
                    struct json_object *jname, *jprice, *jquantity;
                    if (!json_object_object_get_ex(parsed, "name", &jname) ||
                        !json_object_object_get_ex(parsed, "price", &jprice) ||
                        !json_object_object_get_ex(parsed, "quantity", &jquantity)) {
                        response_body = "{\"error\": \"Missing fields\"}";
                        status_code = MHD_HTTP_BAD_REQUEST;
                    } else {
                        // Convert JSON values to appropriate C types
                        // Prepares data for adding to the cart
                        const char *name = json_object_get_string(jname);
                        float price = json_object_get_double(jprice);
                        int quantity = json_object_get_int(jquantity);
                        
                        // Attempt to add item to cart
                        // Handles various error conditions appropriately
                        int result = add_to_plate(name, price, quantity);
                        if (result == -2) {
                            response_body = "{\"error\": \"Invalid price or quantity\"}";
                            status_code = MHD_HTTP_BAD_REQUEST;
                        } else if (result == -1) {
                            response_body = "{\"error\": \"Plate is full\"}";
                            status_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
                        } else {
                            // Return updated bill on successful addition
                            // Provides immediate feedback to the client
                            response_body = generate_bill_json();
                            mem_mode = MHD_RESPMEM_MUST_FREE;
                        }
                    }
                    
                    // Clean up JSON parser resources
                    // Prevents memory leaks from JSON processing
                    json_object_put(parsed);
                }
            }
        } else if (strcmp(url, "/bill") == 0 && strcmp(method, "GET") == 0) {
            // Handle "/bill" endpoint for viewing current cart
            // Returns current bill without modifying cart contents
            response_body = generate_bill_json();
            mem_mode = MHD_RESPMEM_MUST_FREE;
        } else {
            // Handle unknown endpoints with 404 error
            // Provides clear error message for invalid requests
            response_body = "{\"error\": \"Not Found\"}";
            status_code = MHD_HTTP_NOT_FOUND;
        }

        // Create HTTP response with appropriate headers
        // Sends JSON response back to the client
        response = MHD_create_response_from_buffer(strlen(response_body), (void *)response_body, mem_mode);
        MHD_add_response_header(response, "Content-Type", "application/json");
        MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
        ret = MHD_queue_response(connection, status_code, response);
    }

    // Clean up response and connection resources
    // Ensures proper memory management for each request
    MHD_destroy_response(response);

    // Clean up connection info if it exists
    // Frees memory allocated for POST data handling
    if (con_info) {
        free(con_info->post_data);
        free(con_info);
        *con_cls = NULL;
    }

    // Return result to microhttpd framework
    // Indicates success or failure of request processing
    return ret;
}

// Main function - entry point of the application
// Sets up and runs the HTTP server
int main(int argc, char **argv) {
    struct MHD_Daemon *daemon;

    // Start the HTTP daemon with specified configuration
    // Creates a multi-threaded server listening on the defined port
    daemon = MHD_start_daemon(
        MHD_USE_SELECT_INTERNALLY,
        PORT,
        NULL,
        NULL,
        &answer_to_connection,
        NULL,
        MHD_OPTION_END
    );

    // Check if server started successfully
    // Exit with error code if server initialization failed
    if (!daemon)
        return 1;

    // Print server startup information
    // Provides user with connection details and requirements
    printf("Server running at http://localhost:%d\n", PORT);
    printf("Make sure 'pdfbg.png' and 'fonts/Barriecito-Regular.ttf' are in the same folder.\n");
    printf("Press Enter to quit...\n");

    // Wait for user input to stop the server
    // Keeps server running until user decides to quit
    getchar();

    // Stop the HTTP daemon gracefully
    // Ensures proper cleanup of server resources
    MHD_stop_daemon(daemon);
    
    // Exit successfully
    // Returns 0 to indicate normal program termination
    return 0;
}
