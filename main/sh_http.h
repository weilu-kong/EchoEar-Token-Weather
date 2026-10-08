#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define SH_CLOUD_BODY_MAX 16384
typedef struct {const char *name,*value;} sh_http_header_t;
/* HTTPS only; response remains caller-owned, with space for NUL. */
esp_err_t sh_http_request(const char *method,const char *url,const sh_http_header_t *headers,size_t header_count,const char *body,char *response,size_t capacity,int *status,uint32_t network_generation);
