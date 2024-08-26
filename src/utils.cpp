#include "utils.h"




void printMemoryUsage() {
    SERIAL_PRINTF("Free heap: %d, Largest free block: %d\n", 
                  esp_get_free_heap_size(), 
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
