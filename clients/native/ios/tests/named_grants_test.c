#include "runner_host.h"
#include "runner_outlet.h"
#include "runner_switch.h"
#include "runner_apps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned effects;
static int start(unsigned *request) { (void)request; effects++; return 0; }
static int service(const char *command,char *result) { (void)command; (void)result; effects++; return 0; }
int main(void) {
    unsigned grants=25667;
    assert(grants==(RUNNER_GRANT_TIMERS|RUNNER_GRANT_APPS_READ|RUNNER_GRANT_SWITCH_READ|RUNNER_GRANT_OUTLET_READ|RUNNER_GRANT_OUTLET_CONTROL));
    int action,seconds=RUNNER_SWITCH_KEEP;
    const char *reply="";
    assert(runner_switch_request("STATUS",false,false,&seconds,&reply)==0 && !strcmp(reply,"OFF"));
    assert(runner_switch_request("ON 1",(grants&RUNNER_GRANT_SWITCH_CONTROL)!=0,false,&seconds,&reply)==13);
    assert(runner_outlet_request("STATUS",grants,&action,&seconds)==0);
    assert(runner_outlet_request("START 10",grants,&action,&seconds)==0 && seconds==10);
    assert(runner_outlet_request("START 61",grants,&action,&seconds)==4);
    assert(runner_outlet_request("CANCEL",grants,&action,&seconds)==0);
    RunnerAppsInfo info={.name="bench",.active_version=6,.highest_version=6,.state="done"};
    char result[RUNNER_APPS_RESULT_LIMIT];
    assert(runner_apps_command(&info,"STATUS main",grants,start,result)==0);
    assert(runner_apps_command(&info,"RUN main",grants,start,result)==13);
    assert(runner_apps_call(&info,"CALL main outlet CANCEL",grants,service,result)==13);
    assert(!effects);
    puts("Exact named-action grants allow bounded OUTLET control and deny direct relay/app control without effects PASS");
}
