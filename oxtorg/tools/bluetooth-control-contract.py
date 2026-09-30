#!/usr/bin/env python3
"""Exercise the real security initializer with controlled HCI responses.

Run on NetBSD. A second initialization must not rewrite SSP/SC, malformed
readback must fail before any writes, and a rejected enable must propagate.
"""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(sys.argv[1]).resolve()
old = 'SECURE_CONNECTIONS_HOST_SUPPORT_ENABLED' in source.read_text()
wrapper = r'''
#include <sys/types.h>
#include <bluetooth.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int ssp, sc, writes, scenario;
static unsigned char eir[241];
int bt_devopen(const char *name, int flags) { (void)name;(void)flags;return open("/dev/null",O_RDONLY); }
int bt_devreq(int fd, struct bt_devreq *req, time_t timeout) {
 unsigned char *r=req->rparam;size_t length=1;(void)fd;(void)timeout;
 memset(r,0,req->rlen);
 switch(req->opcode) {
 case 0x0c55: r[1]=ssp;length=scenario==2?1:2;break;
 case 0x0c79: r[1]=sc;length=2;break;
 case 0x1004: r[1]=2;r[2]=2;r[3]=0x7f;r[4]=0x0b;length=11;break;
 case 0x0c56: writes++;ssp=1;break;
 case 0x0c7a: writes++;if(scenario==3)r[0]=12;else sc=1;break;
 case 0x0c58: length=2;break;
 case 0x0c52: memcpy(eir,req->cparam,241);break;
 case 0x0c51: memcpy(r+1,eir,241);length=242;break;
 case 0x0c01: break;
 default: fprintf(stderr,"unexpected opcode %04x\n",req->opcode);exit(8);
 }
 req->rlen=length;return 0;
}
#define main control_main
#include "SOURCE_PATH"
#undef main
static void summary(void) { printf("TEST_SECURITY_WRITES %d\n",writes); }
int main(int argc,char **argv) {
 (void)argc;scenario=atoi(argv[1]);ssp=sc=scenario==0;
 atexit(summary);
 char *args[]=ARGS;
 return control_main(ARGC,args);
}
'''
args = '{"control","btuart0","enable",NULL}' if old else '{"control","btuart0","init","Test device",NULL}'
with tempfile.TemporaryDirectory(prefix='bluetooth-control-contract-') as directory:
    path=pathlib.Path(directory)/'contract.c'
    path.write_text(wrapper.replace('SOURCE_PATH',str(source)).replace('ARGS',args).replace('ARGC','3' if old else '4'))
    binary=path.with_suffix('')
    subprocess.run(['cc','-O0',str(path),'-o',str(binary),'-lbluetooth'],check=True)
    failed=False
    for scenario,status,writes,label in [
        (0,0,0,'already enabled preserves SSP/SC'),
        (1,0,2,'cold setup enables SSP before SC'),
        (2,1,0,'short readback refuses writes'),
        (3,1,2,'rejected SC enable is a failure')]:
        result=subprocess.run([str(binary),str(scenario)],text=True,capture_output=True)
        good=result.returncode==status and f'TEST_SECURITY_WRITES {writes}\n' in result.stdout
        print(label+': '+('PASS' if good else 'FAIL'))
        if not good:
            print(result.stdout.strip());print(result.stderr.strip());failed=True
    sys.exit(failed)
