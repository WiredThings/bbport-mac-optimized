"""Explicit opt-in, native admission and inheritance through original pthread shape."""
from pathlib import Path
import subprocess,tempfile
P=Path(__file__).resolve().parents[1]
source=r'''
#include "qa_cpu_qos.h"
#include <cassert>
#include <cstdio>
static int calls;
static int reject(qos_class_t q,int r){++calls;assert(q==QOS_CLASS_USER_INITIATED&&r==0);return EPERM;}
static void* child(void*){
 qos_class_t q;int r;assert(!pthread_get_qos_class_np(pthread_self(),&q,&r));
 printf("child_before_explicit_admission=%u relative=%d\n",unsigned(q),r);
 assert(!bb_qa_cpu_qos_apply(BB_QA_QOS_USER_INITIATED));
 assert(!pthread_get_qos_class_np(pthread_self(),&q,&r));assert(q==QOS_CLASS_USER_INITIATED&&r==0);return nullptr;
}
int main(){
 int mode=-1;assert(!bb_qa_cpu_qos_parse(nullptr,&mode)&&mode==BB_QA_QOS_INHERIT);
 assert(!bb_qa_cpu_qos_parse("inherit",&mode)&&mode==BB_QA_QOS_INHERIT);
 assert(!bb_qa_cpu_qos_apply_with(mode,reject)&&!calls);
 assert(bb_qa_cpu_qos_parse("interactive",&mode)==EINVAL);
 assert(!bb_qa_cpu_qos_parse("user-initiated",&mode)&&mode==BB_QA_QOS_USER_INITIATED);
 assert(bb_qa_cpu_qos_apply_with(mode,reject)==EPERM&&calls==1);
 assert(bb_qa_cpu_qos_apply_with(999,reject)==EINVAL&&calls==1);
 assert(!bb_qa_cpu_qos_apply(mode));qos_class_t q;int r;
 assert(!pthread_get_qos_class_np(pthread_self(),&q,&r));assert(q==QOS_CLASS_USER_INITIATED&&r==0);
 pthread_attr_t attr;assert(!pthread_attr_init(&attr));assert(!pthread_attr_setstacksize(&attr,1024*1024));
 pthread_t t;assert(!pthread_create(&t,&attr,child,nullptr));assert(!pthread_attr_destroy(&attr));assert(!pthread_join(t,nullptr));
}
'''
with tempfile.TemporaryDirectory(prefix='bb-qa-qos-')as d:
 p=Path(d);(p/'test.cpp').write_text(source);subprocess.run(['/usr/bin/clang++','-std=c++20','-O2','-I'+str(P/'src'),str(p/'test.cpp'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
 c=p/'test.c';c.write_text('#include "qa_cpu_qos.h"\nint main(){int mode;return bb_qa_cpu_qos_parse(0,&mode)||bb_qa_cpu_qos_apply(mode);}')
 subprocess.run(['/usr/bin/clang','-std=c11','-I'+str(P/'src'),str(c),'-o',str(p/'test-c')],check=True);subprocess.run([str(p/'test-c')],check=True)
print('PASS default no-op, invalid mode, explicit failure, native admission in both parent and child; C/C++ policy and explicit per-thread admission; inheritance is not assumed')
