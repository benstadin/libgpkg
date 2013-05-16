/*
 * Copyright 2013 Luciad (http://www.luciad.com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdarg.h>
#include <stdio.h>
#include "sqlite.h"
#include "strbuf.h"

int strbuf_init(strbuf_t *buffer, size_t initial_size) {
    void *data = sqlite3_malloc(initial_size);
    if (data == NULL) {
        return SQLITE_NOMEM;
    }

    memset(data, 0, initial_size);

    buffer->buffer = data;
    buffer->capacity = initial_size;
    buffer->length = 0;
    buffer->buffer[buffer->length] = 0;
    return SQLITE_OK;
}

void strbuf_destroy(strbuf_t *buffer) {
    if (buffer == NULL) {
        return;
    }

    if (buffer->buffer) {
      sqlite3_free(buffer->buffer);
      buffer->buffer = NULL;
    }
}

size_t strbuf_length(strbuf_t *buffer) {
    return buffer->length;
}

char *strbuf_data_pointer(strbuf_t *buffer) {
    return buffer->buffer;
}

int strbuf_data(strbuf_t *buffer, char **out) {
    size_t length = strbuf_length(buffer);
    *out = sqlite3_malloc(length + 1);
    if (*out == NULL) {
        return SQLITE_NOMEM;
    } else {
        memmove(*out, buffer->buffer, length);
        (*out)[length] = 0;
        return SQLITE_OK;
    }
}

int strbuf_append(strbuf_t *buffer, const char* msg, ...) {
    int result = SQLITE_OK;
    va_list args;

    va_start(args, msg);
    char* formatted = sqlite3_vmprintf(msg, args);
    va_end(args);
	
	  if (formatted == NULL) {
	      result = SQLITE_NOMEM;
	      goto exit;
	  }

	  size_t formatted_len = strlen(formatted);
	  size_t needed_capacity = buffer->length + formatted_len + 1;
	  if (needed_capacity > buffer->capacity) {
		    size_t new_capacity = buffer->capacity * 3 / 2;
		    if (needed_capacity > new_capacity) {
			      new_capacity = needed_capacity;
		    }
		
		    void *data = sqlite3_realloc(buffer->buffer, new_capacity);
		    if (data == NULL) {
			      result = SQLITE_NOMEM;
            goto exit;
		    }

        memset(data + buffer->capacity, 0, new_capacity - buffer->capacity);

		    buffer->buffer = data;
		    buffer->capacity = new_capacity;
	  }

	  memmove(buffer->buffer + buffer->length, formatted, formatted_len);
	  buffer->length += formatted_len;
    buffer->buffer[buffer->length] = 0;
exit:
    sqlite3_free(formatted);
	
	  return result;
}

