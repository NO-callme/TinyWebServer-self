#include "log/log.h"
#include <cstdio>

int main(){
    Log* log = Log::get_instance();
    if(!log->init("./stress.log", 8192, 10000, 100)){
        printf("log init failed\n");
        return -1;
    }

    //写十万条进行压力测试
    for(int i = 0; i < 100000; i++){
        log->write_log(LOG_INFO,"this is a test log");
    }
    

    log->flush();
    printf("done, check ./test.log\n");
    return 0;
}