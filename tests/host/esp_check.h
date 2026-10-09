#pragma once
#include "esp_err.h"
#define ESP_RETURN_ON_FALSE(a,b,...) do { if(!(a)) return (b); } while(0)
