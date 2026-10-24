// Read-only, noninteractive check of the already-identified PRIVATE QA item.
// Never returns account name/token bytes, never queries the formal service.
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <stdio.h>
int main(void){
 // This throwaway process must never put up a keychain dialog while unattended.
 Boolean oldInteraction=false;
 OSStatus getStatus=SecKeychainGetUserInteractionAllowed(&oldInteraction);
 OSStatus setStatus=SecKeychainSetUserInteractionAllowed(false);
 if(getStatus!=errSecSuccess||setStatus!=errSecSuccess){
  printf("{\"query_attempted\":false,\"get_interaction_status\":%d,\"set_interaction_status\":%d,\"formal_account_queried\":false}\n",(int)getStatus,(int)setStatus);return 1;
 }
 const void* keys[]={kSecClass,kSecAttrService,kSecAttrAccount,kSecReturnData,kSecMatchLimit,kSecUseAuthenticationUI};
 const void* values[]={kSecClassGenericPassword,CFSTR("io.github.bbport.optimized.options-qa.online.huntersdream"),CFSTR("device"),kCFBooleanTrue,kSecMatchLimitOne,kSecUseAuthenticationUIFail};
 CFDictionaryRef query=CFDictionaryCreate(kCFAllocatorDefault,keys,values,6,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 CFTypeRef result=NULL;OSStatus status=SecItemCopyMatching(query,&result);
 int available=status==errSecSuccess&&result&&CFGetTypeID(result)==CFDataGetTypeID()&&CFDataGetLength((CFDataRef)result)>0;
 printf("{\"status\":%d,\"private_qa_data_available\":%s,\"prompt_allowed\":false,\"formal_account_queried\":false,\"credentials_printed\":false}\n",(int)status,available?"true":"false");
 if(result)CFRelease(result);CFRelease(query);SecKeychainSetUserInteractionAllowed(oldInteraction);return 0;
}
