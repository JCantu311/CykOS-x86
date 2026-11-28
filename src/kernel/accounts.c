#include "accounts.h"
#include "permissions.h"
#include "vfs.h"
#include "cykastd.h"

#define ACCOUNT_LIMIT 32
#define ACCOUNT_MAGIC 0x43594131u
#define ACCOUNT_DB "/ETC/ACCOUNTS.DB"
#define PASSWORD_SALT_SIZE 16
#define PASSWORD_HASH_SIZE 32
#define PASSWORD_ROUNDS 1024

typedef struct { unsigned int magic, count; } account_header_t;
typedef struct {
    char name[ACCOUNT_NAME_MAX];
    unsigned int uid, gid, flags, has_password;
    unsigned char salt[PASSWORD_SALT_SIZE];
    unsigned char hash[PASSWORD_HASH_SIZE];
} account_record_t;

static account_record_t g_accounts[ACCOUNT_LIMIT];
static unsigned int g_count;
static unsigned int g_salt_counter;

/* Compact SHA-256 implementation used only for stored password verifiers. */
static const unsigned int K[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};
static unsigned int rotr(unsigned int x, unsigned int n) { return (x >> n) | (x << (32 - n)); }

/* Hash via explicit padded input, avoiding special cases in the block routine. */
static void sha256_password(const unsigned char* data, unsigned int length, unsigned char out[32]) {
    unsigned char padded[128]; unsigned int total = length + 1;
    memcpy(padded, data, length); padded[length] = 0x80;
    while ((total % 64u) != 56u) padded[total++] = 0;
    unsigned long long bits = (unsigned long long)length * 8ull;
    for (int i=7;i>=0;i--) padded[total++]=(unsigned char)(bits>>(i*8));
    /* Feed complete blocks through the regular transform with a wrapper. */
    unsigned int h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for (unsigned int off=0; off<total; off+=64) {
        unsigned int w[64];
        for(unsigned int i=0;i<16;i++) w[i]=((unsigned int)padded[off+i*4]<<24)|((unsigned int)padded[off+i*4+1]<<16)|((unsigned int)padded[off+i*4+2]<<8)|padded[off+i*4+3];
        for(unsigned int i=16;i<64;i++){unsigned int s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3),s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
        unsigned int a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],x=h[7];
        for(unsigned int i=0;i<64;i++){unsigned int s1=rotr(e,6)^rotr(e,11)^rotr(e,25),ch=(e&f)^(~e&g),t1=x+s1+ch+K[i]+w[i],s0=rotr(a,2)^rotr(a,13)^rotr(a,22),maj=(a&b)^(a&c)^(b&c),t2=s0+maj;x=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=x;
    }
    for(unsigned int i=0;i<8;i++){out[i*4]=(unsigned char)(h[i]>>24);out[i*4+1]=(unsigned char)(h[i]>>16);out[i*4+2]=(unsigned char)(h[i]>>8);out[i*4+3]=(unsigned char)h[i];}
}

static int name_equal(const char* a, const char* b) {
    unsigned int i=0; for(; i<ACCOUNT_NAME_MAX && a[i] && b[i]; i++) {
        char x=a[i], y=b[i]; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32;
        if(x!=y)return 0;
    }
    return i<ACCOUNT_NAME_MAX && a[i]==b[i];
}
static int valid_name(const char* name) {
    unsigned int n=0; if(!name)return 0;
    for(;n<ACCOUNT_NAME_MAX;n++){if(!name[n])return n>0;char c=name[n];if(n>=ACCOUNT_NAME_MAX-1)return 0;
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return 0;}
    return 0;
}
static int find_account(const char* name) { for(unsigned int i=0;i<g_count;i++)if(name_equal(g_accounts[i].name,name))return (int)i;return -1; }
static int persist_accounts(void) {
    unsigned char data[sizeof(account_header_t)+sizeof(g_accounts)];
    account_header_t h={ACCOUNT_MAGIC,g_count}; unsigned int bytes=sizeof(h)+g_count*sizeof(account_record_t);
    memcpy(data,&h,sizeof(h));memcpy(data+sizeof(h),g_accounts,g_count*sizeof(account_record_t));
    if (vfs_write_file(ACCOUNT_DB,data,bytes) != 0) return -1;
    /* Keep password verifiers readable only by root. */
    return permissions_chmod(ACCOUNT_DB, 0600u);
}
static void password_digest(const char* password,const unsigned char salt[16],unsigned char out[32]) {
    unsigned char buffer[80], digest[32]; unsigned int length=(unsigned int)strlen(password);
    memcpy(buffer,salt,16);memcpy(buffer+16,password,length);sha256_password(buffer,16+length,digest);
    for(unsigned int i=1;i<PASSWORD_ROUNDS;i++){memcpy(buffer,salt,16);memcpy(buffer+16,digest,32);sha256_password(buffer,48,digest);}
    memcpy(out,digest,32);
}
void accounts_init(void) {
    g_count=0;memset(g_accounts,0,sizeof(g_accounts));
    unsigned char data[sizeof(account_header_t)+sizeof(g_accounts)]; account_header_t h;
    int bytes=vfs_read_file_by_name(ACCOUNT_DB,data,sizeof(data));
    if(bytes>=(int)sizeof(h)) {memcpy(&h,data,sizeof(h));if(h.magic==ACCOUNT_MAGIC&&h.count<=ACCOUNT_LIMIT&&bytes>=(int)(sizeof(h)+h.count*sizeof(account_record_t))){
        account_record_t* in=(account_record_t*)(data+sizeof(h));
        for(unsigned int i=0;i<h.count;i++)if(valid_name(in[i].name)&&in[i].uid<=0x7fffffffu&&in[i].has_password<=1&&find_account(in[i].name)<0)g_accounts[g_count++]=in[i];
    }}
    if(find_account("root")<0&&g_count<ACCOUNT_LIMIT){account_record_t* a=&g_accounts[g_count++];memset(a,0,sizeof(*a));memcpy(a->name,"root",5);a->uid=0;a->gid=0;a->flags=ACCOUNT_FLAG_SUDO;(void)persist_accounts();}
}
unsigned int accounts_count(void){return g_count;}
int accounts_get(unsigned int i,char name[ACCOUNT_NAME_MAX],unsigned int* uid,unsigned int* gid,unsigned int* flags){if(i>=g_count)return -1;if(name)memcpy(name,g_accounts[i].name,ACCOUNT_NAME_MAX);if(uid)*uid=g_accounts[i].uid;if(gid)*gid=g_accounts[i].gid;if(flags)*flags=g_accounts[i].flags;return 0;}
int accounts_lookup(const char* name,unsigned int* uid,unsigned int* gid,unsigned int* flags){int i=find_account(name);if(i<0)return -1;if(uid)*uid=g_accounts[i].uid;if(gid)*gid=g_accounts[i].gid;if(flags)*flags=g_accounts[i].flags;return 0;}
int accounts_create(const char* name,unsigned int uid,unsigned int gid,unsigned int flags){if(permissions_uid()!=0||!valid_name(name)||uid==0||uid>0x7fffffffu||find_account(name)>=0||g_count>=ACCOUNT_LIMIT)return -1;for(unsigned int i=0;i<g_count;i++)if(g_accounts[i].uid==uid)return -1;account_record_t a;memset(&a,0,sizeof(a));memcpy(a.name,name,strlen(name)+1);a.uid=uid;a.gid=gid;a.flags=flags&ACCOUNT_FLAG_SUDO;g_accounts[g_count++]=a;if(persist_accounts()!=0){g_count--;memset(&g_accounts[g_count],0,sizeof(a));return -1;}return 0;}
int accounts_delete(const char* name){if(permissions_uid()!=0)return -1;int i=find_account(name);if(i<0||g_accounts[i].uid==0)return -1;account_record_t removed=g_accounts[i];for(unsigned int j=(unsigned int)i+1;j<g_count;j++)g_accounts[j-1]=g_accounts[j];g_count--;memset(&g_accounts[g_count],0,sizeof(removed));if(persist_accounts()!=0){for(unsigned int j=g_count;j>(unsigned int)i;j--)g_accounts[j]=g_accounts[j-1];g_accounts[i]=removed;g_count++;return -1;}return 0;}
int accounts_set_flags(const char* name,unsigned int flags){if(permissions_uid()!=0)return -1;int i=find_account(name);if(i<0||g_accounts[i].uid==0)return -1;unsigned int old=g_accounts[i].flags;g_accounts[i].flags=flags&ACCOUNT_FLAG_SUDO;if(persist_accounts()!=0){g_accounts[i].flags=old;return -1;}return 0;}
int accounts_set_password(const char* name,const char* password){int i=find_account(name);if(i<0||!password)return -1;unsigned int len=(unsigned int)strlen(password);if(!len||len>ACCOUNT_PASSWORD_MAX)return -1;if(permissions_uid()!=0&&permissions_uid()!=g_accounts[i].uid)return -1;account_record_t old=g_accounts[i];unsigned int tsc_lo=0,tsc_hi=0;__asm__ volatile("rdtsc":"=a"(tsc_lo),"=d"(tsc_hi));unsigned int seed[4]={tsc_lo,tsc_hi,++g_salt_counter,g_accounts[i].uid};for(unsigned int j=0;j<16;j++)g_accounts[i].salt[j]=(unsigned char)(seed[j%4]>>(8*(j%4)));password_digest(password,g_accounts[i].salt,g_accounts[i].hash);g_accounts[i].has_password=1;if(persist_accounts()!=0){g_accounts[i]=old;return -1;}return 0;}
int accounts_has_password(const char* name){int i=find_account(name);if(i<0)return -1;return g_accounts[i].has_password?1:0;}
int accounts_authenticate(const char* name,const char* password){int i=find_account(name);if(i<0||!password||!g_accounts[i].has_password)return -1;unsigned char digest[32];password_digest(password,g_accounts[i].salt,digest);unsigned int diff=0;for(unsigned int j=0;j<32;j++)diff|=(unsigned int)(digest[j]^g_accounts[i].hash[j]);return diff? -1:0;}
