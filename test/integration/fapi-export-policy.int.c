/* SPDX-License-Identifier: BSD-2-Clause */
/*******************************************************************************
 * Copyright 2017-2018, Fraunhofer SIT sponsored by Infineon Technologies AG
 * All rights reserved.
 *******************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <json-c/json.h>
#include <json-c/json_util.h>

#include "tss2_fapi.h"
#include "tss2_esys.h"
#include "tss2_tcti.h"

#include "test-fapi.h"

#define LOGMODULE test
#include "util/log.h"
#include "util/aux_util.h"


typedef struct {
    char *path;
    char *sha1;
    char *sha256;
} policy_digests;


/** Check the digest values from result table for sha1 and sha256. */
static bool
check_policy(char *policy, policy_digests *digests) {
    json_object *jso = NULL;
    json_object *jso_digest_struct = NULL;
    json_object *jso_digest_list = NULL;
    json_object *jso_digest = NULL;
    const char *digest_str= NULL;
    bool check_sha1 = false;
    bool check_sha256 = false;
    int i, n;

    jso = json_tokener_parse(policy);
    if (!jso) {
        LOG_ERROR("JSON error in policy");
        goto error;
    }
    if (!json_object_object_get_ex(jso, "policyDigests", &jso_digest_list)) {
        LOG_ERROR("Policy error");
        goto error;
    }
    n = json_object_array_length(jso_digest_list);
    if (n > 0) {
        if (!digests->sha1 || !digests->sha256) {
            LOG_ERROR("Digest computation for %s should not be possible.", digests->path);
            goto error;
        }
    }
    for (i = 0; i < n; i++) {
        jso_digest_struct = json_object_array_get_idx(jso_digest_list, i);
        if (!jso_digest_struct) {
            LOG_ERROR("Policy error");
            goto error;
        }
        if (!json_object_object_get_ex(jso_digest_struct, "digest", &jso_digest)) {
            LOG_ERROR("Policy error2");
            goto error;
        }

        digest_str = json_object_get_string(jso_digest);
        if (strlen(digest_str) == 40) {
            LOG_INFO("Digest SHA1: %s", digest_str);
            if (strcmp(digest_str, digests->sha1) == 0) {
                check_sha1 = true;
            }
        } else if  (strlen(digest_str) == 64) {
            LOG_INFO("Digest SHA256: %s", digest_str);
            if (strcmp(digest_str, digests->sha256) == 0) {
                check_sha256 = true;
            }
        } else {
            LOG_WARNING("Hash alg not in result table.");
        }
    }
    json_object_put(jso);
    if (n > 0 && (!check_sha1 || !check_sha256)) {
        LOG_ERROR("Policy check failed for: %s", digests->path);
        goto error;
    }
    return true;
 error:
    if (jso)
        json_object_put(jso);
    return false;
}

static char *
read_policy(FAPI_CONTEXT *context, char *policy_name)
{
    FILE *stream = NULL;
    long policy_size;
    char *json_policy = NULL;
    char policy_file[1024];

    if (snprintf(&policy_file[0], 1023, TOP_SOURCEDIR "/test/data/fapi/%s.json", policy_name) < 0)
        return NULL;

    stream = fopen(policy_file, "r");
    if (!stream) {
        LOG_ERROR("File %s does not exist", policy_file);
        return NULL;
    }
    fseek(stream, 0L, SEEK_END);
    policy_size = ftell(stream);
    fclose(stream);
    json_policy = malloc(policy_size + 1);
    stream = fopen(policy_file, "r");
    ssize_t ret = read(fileno(stream), json_policy, policy_size);
    if (ret != policy_size) {
        LOG_ERROR("IO error %s.", policy_file);
        return NULL;
    }
    json_policy[policy_size] = '\0';
    return json_policy;
}

/** Test the FAPI key signing with PolicyAuthorizeNV.
 *
 * Tested FAPI commands:
 *  - Fapi_Provision()
 *  - Fapi_Import()
 *  - Fapi_ExportPolicy()
 *
 * @param[in,out] context The FAPI_CONTEXT.
 * @retval EXIT_FAILURE
 * @retval EXIT_SUCCESS
 */
int
test_fapi_export_policy(FAPI_CONTEXT *context)
{
    TSS2_RC r;
    size_t i;

    /*
     * Table with expected policy digests.
     * If computation is not possible sha1 and sha256 has to be set to NULL.
     * If a policy digest will be computed for these cases an error will be signalled.
     */
    static policy_digests policies[] = {
        { .path = "/policy/pol_action",
          .sha1 = "0000000000000000000000000000000000000000",
          .sha256 = "0000000000000000000000000000000000000000000000000000000000000000" },
        { .path = "/policy/pol_pcr16_0_ecc_authorized",
          .sha1 = "eab0d71ae6088009cbd0b50729fde69eb453649c",
          .sha256 = "bff2d58e9813f97cefc14f72ad8133bc7092d652b7c877959254af140c841f36" },
        { .path = "/policy/pol_authorize_ecc_pem",
          .sha1 = "bf566648f8842b5eda9b1900804c7b151b01df28",
          .sha256 = "a1eecf1da5fe2f66457af9fd06e6b23b10440da0d593914ff3f38533cafe218e" },
        { .path = "/policy/pol_nv_counter", .sha1 = NULL, .sha256 = NULL },
        { .path = "/policy/pol_authorize_rsa_pem",
          .sha1 = "47ae64686921c18c71c97704d9d728618c9a3ffc",
          .sha256 = "a91f9dce9c7a913ffa21bfdf70ff3ccd62231ca1041722fad4ac89f789458207" },
        { .path = "/policy/pol_locality",
          .sha1 = "9d2af7c7235047d90719bb07e699bc266554997f",
          .sha256 = "ddee6af14bf3c4e8127ced87bcf9a57e1c0c8ddb5e67735c8505f96f07b8dbb8" },
        { .path = "/policy/pol_nv_change_auth",
          .sha1 = "9ebf6fd0f5547da6c57280ae4032c2de62b773da",
          .sha256 = "363ac945b6457c47c31f3355dba0db27de8db213d6250c6bf79685003f9fe7ab" },
        { .path = "/policy/pol_password",
          .sha1 = "af6038c78c5c962d37127e319124e3a8dc582e9b",
          .sha256 = "8fcd2169ab92694e0c633f1ab772842b8241bbc20288981fc7ac1eddc1fddb0e" },
        { .path = "/policy/pol_pcr16_0_or",
          .sha1 = "be5ea4b5e7de56f883968667f1f12f02b7c3c99b",
          .sha256 = "04b01d728fc1ea060d943b3ca6e3e5ea9d3bbb61126542677ad7591c092eafba" },
        { .path = "/policy/pol_physical_presence",
          .sha1 = "9acb06395f831f88e89eeac29442cb0ebe9485ab",
          .sha256 = "0d7c6747b1b9facbba03492097aa9d5af792e5efc07346e05f9daa8b3d9e13b5" },
        { .path = "/policy/pol_secret", .sha1 = NULL, .sha256 = NULL },
        { .path = "/policy/pol_authorize", .sha1 = NULL, .sha256 = NULL },
        { .path = "/policy/pol_authorize_nv", .sha1 = NULL, .sha256 = NULL },
        { .path = "/policy/pol_auth_value",
          .sha1 = "af6038c78c5c962d37127e319124e3a8dc582e9b",
          .sha256 = "8fcd2169ab92694e0c633f1ab772842b8241bbc20288981fc7ac1eddc1fddb0e" },
        { .path = "/policy/pol_command_code",
          .sha1 = "2a2a1493809bbc1b4b46fc325dc54a815cbb980e",
          .sha256 = "cc6918b226273b08f5bd406d7f10cf160f0a7d13dfd83b7770ccbcd1aa80d811" },
        { .path = "/policy/pol_duplicate", .sha1 = NULL, .sha256 = NULL },
        { .path = "/policy/pol_pcr16_0",
          .sha1 = "eab0d71ae6088009cbd0b50729fde69eb453649c",
          .sha256 = "bff2d58e9813f97cefc14f72ad8133bc7092d652b7c877959254af140c841f36" },
        { .path = "/policy/pol_nv", .sha1 = NULL, .sha256 = NULL },
        { .path = "/policy/pol_authorize_outer", .sha1 = NULL, .sha256 = NULL },
        { .path = "/policy/pol_countertimer",
          .sha1 = "969a04fb820cc4a3aa4436e02cd5a71a87fd95b9",
          .sha256 = "7c67802209683d17c1d94f3fc9df7afb2a0d7955c3c5d0fa3f602d58ffdaf984" },
        { .path = "/policy/pol_cphash",
          .sha1 = "b2b2763b9a638a8e4f38897a47468e09fe0a0853",
          .sha256 = "2d7038734b12258ae7108ab70d0e7ee36f4e64c64d53f8adb6c2bed602c95d09" },
        { .path = "/policy/pol_name_hash", .sha1 = NULL, .sha256 = NULL },
        { .path = "/policy/pol_nv_written",
          .sha1 = "5a91e7105386bd547a15aad40369b1e25e462873",
          .sha256 = "3c326323670e28ad37bd57f63b4cc34d26ab205ef22f275c58d47fab2485466e" },
        { .path = "/policy/pol_pcr16_0_fail",
          .sha1 = "904fbcef1d6cb3ecaf085259b40b55fa3025232f",
          .sha256 = "b740077197d46009b9c18f5ad181b7a3ac5bef1d9a881cc5dde808f1a6b8c787" },
        { .path = "/policy/pol_pcr16_read",
          .sha1 = "eab0d71ae6088009cbd0b50729fde69eb453649c",
          .sha256 = "bff2d58e9813f97cefc14f72ad8133bc7092d652b7c877959254af140c841f36" },
        { .path = "/policy/pol_pcr8_0",
          .sha1 = "d6756ffbe88b1d082ee7048500ff9cc1a718de40",
          .sha256 = "2a90ac03196573f129e70a9e04485bff581d2890fe5882d3c2667290d84b497b" },
        { .path = "/policy/pol_signed_ecc",
          .sha1 = "c18fc6f175bd17e41c257184443a81c99ced9225",
          .sha256 = "07aefc36fb098f5f59f2f74d8854235a29d1f93b4ddd488f6ec667d9c1d716b6" },
        { .path = "/policy/pol_signed",
          .sha1 = "85bf0403e87d3c29c3daa5c87efb111cb717875d",
          .sha256 = "b1969529af7796c5b4f5e4781713f4525049f36cb12ec63f996dad1c4401c068" },
    };

    char *json_policy = NULL;
    char *policy = NULL;

    r = Fapi_Provision(context, NULL, NULL, NULL);
    goto_if_error(r, "Error Fapi_Provision", error);

    for (i = 0; i < sizeof(policies) / sizeof(policies[0]); i++) {
        fprintf(stderr, "\nTest policy: %s\n",  policies[i].path);
        json_policy = read_policy(context, policies[i].path);
        if (!json_policy)
            goto error;

        r = Fapi_Import(context, policies[i].path, json_policy);
        goto_if_error(r, "Error Fapi_Import", error);

        policy = NULL;
        r = Fapi_ExportPolicy(context, policies[i].path, &policy);
        goto_if_error(r, "Error Fapi_ExportPolicy", error);
        ASSERT(policy != NULL);
        ASSERT(strlen(policy) > ASSERT_SIZE);
        if (!check_policy(policy, &policies[i])) {
            goto error;
        }
        fprintf(stderr, "\nPolicy from policy file:\n%s\n%s\n", policies[i].path, policy);

        SAFE_FREE(json_policy);
        SAFE_FREE(policy);
    }

    r = Fapi_Delete(context, "/");
    goto_if_error(r, "Error Fapi_Delete", error);

    return EXIT_SUCCESS;

error:
    Fapi_Delete(context, "/");
    SAFE_FREE(json_policy);
    SAFE_FREE(policy);
    return EXIT_FAILURE;
}

int
test_invoke_fapi(FAPI_CONTEXT *context)
{
    return test_fapi_export_policy(context);
}
