/*
 * %CopyrightBegin%
 *
 * Copyright Ericsson AB 2010-2018. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */

#include "block.h"
#include "aes.h"
#include "cipher.h"

ERL_NIF_TERM block_crypt_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{/* (Type, Key, Ivec, Text, IsEncrypt) or (Type, Key, Text, IsEncrypt) */
    struct cipher_type_t *cipherp = NULL;
    const EVP_CIPHER     *cipher;
    ErlNifBinary         key, ivec, text;
    EVP_CIPHER_CTX*      ctx;
    ERL_NIF_TERM         ret;
    unsigned char        *out;
    int                  ivec_size, out_size = 0;

    if (!enif_inspect_iolist_as_binary(env, argv[1], &key)
        || !(cipherp = get_cipher_type(argv[0], key.size))
        || !enif_inspect_iolist_as_binary(env, argv[argc - 2], &text)) {
        return enif_make_badarg(env);
    }
    cipher = cipherp->cipher.p;
    if (!cipher) {
        return enif_raise_exception(env, atom_notsup);
    }

    if (argv[0] == atom_aes_cfb8
        && (key.size == 24 || key.size == 32)) {
        /* Why do EVP_CIPHER_CTX_set_key_length() fail on these key sizes?
         * Fall back on low level API
         */
        return aes_cfb_8_crypt(env, argc-1, argv+1);
    }
    else if (argv[0] == atom_aes_cfb128
        && (key.size == 24 || key.size == 32)) {
        /* Why do EVP_CIPHER_CTX_set_key_length() fail on these key sizes?
         * Fall back on low level API
         */
        return aes_cfb_128_crypt_nif(env, argc-1, argv+1);
   }

    ivec_size  = EVP_CIPHER_iv_length(cipher);

#ifdef HAVE_ECB_IVEC_BUG
    if (argv[0] == atom_aes_ecb || argv[0] == atom_blowfish_ecb ||
	argv[0] == atom_des_ecb)
	ivec_size = 0; /* 0.9.8l returns faulty ivec_size */
#endif

    if (text.size % EVP_CIPHER_block_size(cipher) != 0 ||
        (ivec_size == 0 ? argc != 4
                      : (argc != 5 ||
                         !enif_inspect_iolist_as_binary(env, argv[2], &ivec) ||
                         ivec.size != ivec_size))) {
        return enif_make_badarg(env);
    }

    out = enif_make_new_binary(env, text.size, &ret);

    ctx = EVP_CIPHER_CTX_new();
    if (!EVP_CipherInit_ex(ctx, cipher, NULL, NULL, NULL,
                           (argv[argc - 1] == atom_true)) ||
        !EVP_CIPHER_CTX_set_key_length(ctx, key.size) ||
        !(EVP_CIPHER_type(cipher) != NID_rc2_cbc ||
          EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_SET_RC2_KEY_BITS, key.size * 8, NULL)) ||
        !EVP_CipherInit_ex(ctx, NULL, NULL,
                           key.data, ivec_size ? ivec.data : NULL, -1) ||
        !EVP_CIPHER_CTX_set_padding(ctx, 0)) {

        EVP_CIPHER_CTX_free(ctx);
        return enif_raise_exception(env, atom_notsup);
    }

    if (text.size > 0 && /* OpenSSL 0.9.8h asserts text.size > 0 */
        (!EVP_CipherUpdate(ctx, out, &out_size, text.data, text.size)
         || (ASSERT(out_size == text.size), 0)
         || !EVP_CipherFinal_ex(ctx, out + out_size, &out_size))) {

        EVP_CIPHER_CTX_free(ctx);
        return enif_raise_exception(env, atom_notsup);
    }
    ASSERT(out_size == 0);
    EVP_CIPHER_CTX_free(ctx);
    CONSUME_REDS(env, text);

    return ret;
}
