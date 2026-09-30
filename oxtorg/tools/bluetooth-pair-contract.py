#!/usr/bin/env python3
"""Run the real pairing event parser on NetBSD with literal HCI packets.

Only the HCI command transport is replaced. These cases catch confirming
another peer, trusting another handle, malformed events and insecure success.
"""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(sys.argv[1]).resolve()
if not source.is_file():
    print("FAIL: native pairing utility is missing")
    sys.exit(1)

wrapper = r'''
#include <sys/types.h>
#include <bluetooth.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int sent;
static uint16_t opcode;
static unsigned char command_data[9];
ssize_t bt_devsend(int fd, uint16_t op, void *data, size_t size) {
    (void)fd;
    if (size > sizeof(command_data)) abort();
    sent++; opcode=op; memcpy(command_data,data,size); return size;
}
#define main pair_main
#include "SOURCE_PATH"
#undef main
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while (0)
int main(void) {
    struct pairing p={0};
    uint8_t connect_event[]={4,3,11,0,11,0,6,5,4,3,2,1,1,0};
    uint8_t capability[]={4,0x31,6,6,5,4,3,2,1};
    uint8_t confirmation[]={4,0x33,10,6,5,4,3,2,1,0x40,0xe2,1,0};
    uint8_t auth[]={4,6,3,0,11,0};
    uint8_t encryption[]={4,8,4,0,11,0,2};
    uint8_t key[26]={4,0x18,23,6,5,4,3,2,1};
    uint8_t reply[]={3,0x4b,0x56,0,9,0,2,0,1,0,1,0,0,0};
    CHECK(bt_aton("01:02:03:04:05:06", &p.peer));
    capability[3]=99;
    CHECK(pair_event(&p,-1,capability,sizeof(capability))==0 && sent==0);
    capability[3]=6;
    CHECK(pair_event(&p,-1,capability,sizeof(capability))==0 && sent==1);
    CHECK(opcode==0x042b && memcmp(command_data,capability+3,6)==0);
    CHECK(command_data[6]==1 && command_data[7]==0 && command_data[8]==3);
    puts("selected peer capabilities: PASS");
    confirmation[3]=99;
    CHECK(pair_event(&p,-1,confirmation,sizeof(confirmation))==0 && !p.pending);
    confirmation[3]=6;
    CHECK(pair_event(&p,-1,confirmation,sizeof(confirmation))==0);
    CHECK(p.pending && p.number==123456);
    CHECK(pair_confirm(&p,-1,0)==0 && opcode==0x042d && !p.pending);
    CHECK(pair_event(&p,-1,confirmation,sizeof(confirmation))==0);
    CHECK(pair_confirm(&p,-1,1)==0 && opcode==0x042c && !p.pending);
    CHECK(pair_confirm(&p,-1,1)==-1);
    puts("explicit confirmation and rejection: PASS");
    CHECK(pair_event(&p,-1,connect_event,sizeof(connect_event)-1)==-1);
    CHECK(pair_event(&p,-1,connect_event,sizeof(connect_event))==0);
    CHECK(p.have_handle && p.handle==11 && !pair_secured(&p));
    auth[4]=12;
    CHECK(pair_event(&p,-1,auth,sizeof(auth))==0 && !p.authenticated);
    auth[4]=11;
    CHECK(pair_event(&p,-1,auth,sizeof(auth))==0 && !pair_secured(&p));
    encryption[4]=12;
    CHECK(pair_event(&p,-1,encryption,sizeof(encryption))==0 && !pair_secured(&p));
    encryption[4]=11;
    CHECK(pair_event(&p,-1,encryption,sizeof(encryption))==0 && pair_secured(&p));
    puts("target handle authentication and AES-CCM: PASS");
    encryption[6]=1;
    CHECK(pair_event(&p,-1,encryption,sizeof(encryption))==-1 && !pair_secured(&p));
    auth[3]=5;
    CHECK(pair_event(&p,-1,auth,sizeof(auth))==-1 && !p.authenticated);
    puts("authentication failure and legacy encryption rejected: PASS");
    memset(key+9,0xaa,16); key[25]=8;
    CHECK(pair_event(&p,-1,key,sizeof(key))==0 && p.key_type==8);
    key[25]=4;
    CHECK(pair_event(&p,-1,key,sizeof(key))==-1);
    puts("authenticated P-256 key metadata: PASS");
    p.just_works=1;
    CHECK(pair_event(&p,-1,capability,sizeof(capability))==0);
    CHECK(command_data[6]==3 && command_data[8]==2);
    sent=0; confirmation[3]=99;
    CHECK(pair_event(&p,-1,confirmation,sizeof(confirmation))==0 && sent==0);
    confirmation[3]=6;
    CHECK(pair_event(&p,-1,confirmation,sizeof(confirmation))==0);
    CHECK(sent==1 && opcode==0x042c && !p.pending);
    key[25]=7;
    CHECK(pair_event(&p,-1,key,sizeof(key))==0);
    key[25]=4;
    CHECK(pair_event(&p,-1,key,sizeof(key))==-1);
    puts("explicit Just Works only for selected peer, P-256 required: PASS");
    p.bonded_only=1; sent=0;
    CHECK(pair_event(&p,-1,capability,sizeof(capability))==-1 && sent==0);
    auth[3]=0;
    CHECK(pair_event(&p,-1,auth,sizeof(auth))==0);
    CHECK(pair_event(&p,-1,encryption,sizeof(encryption))==0 && pair_secured(&p));
    p.bonded_only=0;
    puts("bonded-only mode does not initiate new pairing: PASS");
    p.just_works=0; p.legacy=1;
    key[25]=0;
    CHECK(pair_event(&p,-1,key,sizeof(key))==0);
    auth[3]=0;
    CHECK(pair_event(&p,-1,auth,sizeof(auth))==0);
    CHECK(pair_event(&p,-1,encryption,sizeof(encryption))==0 && pair_secured(&p));
    key[25]=3;
    CHECK(pair_event(&p,-1,key,sizeof(key))==-1);
    puts("explicit legacy PIN permits E0, rejects debug key: PASS");
    CHECK(sdp_reply_valid(reply,sizeof(reply)));
    CHECK(!sdp_reply_valid(reply,sizeof(reply)-1));
    reply[2]=0x57; CHECK(!sdp_reply_valid(reply,sizeof(reply))); reply[2]=0x56;
    reply[8]=2; CHECK(!sdp_reply_valid(reply,sizeof(reply)));
    puts("public SDP response framing and transaction: PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="bluetooth-pair-contract-") as directory:
    path = pathlib.Path(directory) / "contract.c"
    path.write_text(wrapper.replace("SOURCE_PATH", str(source)))
    binary = path.with_suffix("")
    subprocess.run(["cc", "-O0", "-Wall", "-Wextra", "-Werror", str(path),
                    "-o", str(binary), "-lbluetooth"], check=True)
    result = subprocess.run([str(binary)], text=True, capture_output=True)
    if "aa" * 16 in result.stdout.lower():
        raise SystemExit("FAIL: link key leaked to stdout")
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    sys.exit(result.returncode)
