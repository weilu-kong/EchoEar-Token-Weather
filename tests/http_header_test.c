#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http_header.h"
int main(void){
    const int lengths[]={1732,4096};
    for(int i=0;i<2;i++){
        char *value=malloc(lengths[i]+8),buffer[4608];assert(value);
        memcpy(value,"Bearer ",7);memset(value+7,'A',lengths[i]);value[lengths[i]+7]=0;
        http_header_handle_t headers=http_header_init();assert(headers);
        assert(http_header_set(headers,"Authorization",value)==ESP_OK);
        int n=512;assert(http_header_generate_string(headers,0,buffer,&n)==0&&n==0);
        n=sizeof(buffer);assert(http_header_generate_string(headers,0,buffer,&n)==1&&n>lengths[i]);
        assert(strstr(buffer,"Authorization: Bearer ")==buffer);
        http_header_destroy(headers);free(value);
    }
    puts("SDK header check: old 512-byte buffer fails; new buffer carries actual-size and maximum OAuth headers");
}
