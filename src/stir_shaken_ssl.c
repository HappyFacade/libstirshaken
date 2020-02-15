#include "stir_shaken.h"


static int do_sign_init(EVP_MD_CTX *ctx, EVP_PKEY *pkey,
		const EVP_MD *md, STACK_OF(OPENSSL_STRING) *sigopts)
{
	EVP_PKEY_CTX    *pkctx = NULL;
	int             def_nid = 0;

	if (ctx == NULL)
		return 0;
	/*
	 * EVP_PKEY_get_default_digest_nid() returns 2 if the digest is mandatory
	 * for this algorithm.
	 */
	if (EVP_PKEY_get_default_digest_nid(pkey, &def_nid) == 2
			&& def_nid == NID_undef) {
		/* The signing algorithm requires there to be no digest */
		md = NULL;
	}
	if (!EVP_DigestSignInit(ctx, &pkctx, md, NULL, pkey))
		return 0;

	/** Not for now
	  for (i = 0; i < sk_OPENSSL_STRING_num(sigopts); i++) {
	  char *sigopt = sk_OPENSSL_STRING_value(sigopts, i);
	  if (pkey_ctrl_string(pkctx, sigopt) <= 0) {
	  BIO_printf(bio_err, "parameter error \"%s\"\n", sigopt);
	  ERR_print_errors(bio_err);
	  return 0;
	  }
	  }**/
	return 1;
}

int do_X509_REQ_sign(X509_REQ *x, EVP_PKEY *pkey, const EVP_MD *md,
		STACK_OF(OPENSSL_STRING) *sigopts)
{
	int rv;

#if OPENSSL_VERSION_MINOR < 1
	EVP_MD_CTX *mctx = EVP_MD_CTX_create();
#else
	EVP_MD_CTX *mctx = EVP_MD_CTX_new();
#endif
	EVP_MD_CTX_init(mctx);
	rv = do_sign_init(mctx, pkey, md, sigopts);
	if (rv > 0)
		rv = X509_REQ_sign_ctx(x, mctx);
#if OPENSSL_VERSION_MINOR < 1
	EVP_MD_CTX_destroy(mctx);
#else
	EVP_MD_CTX_free(mctx);
#endif
	return rv > 0 ? 1 : 0;
}

static unsigned char *generic_asn1(const char *value, X509V3_CTX *ctx,
		long *ext_len)
{
	ASN1_TYPE *typ;
	unsigned char *ext_der = NULL;
	typ = ASN1_generate_v3((char*)value, ctx);
	if (typ == NULL)
		return NULL;
	*ext_len = i2d_ASN1_TYPE(typ, &ext_der);
	ASN1_TYPE_free(typ);
	return ext_der;
}
/**
  static void delete_ext(STACK_OF(X509_EXTENSION) *sk, X509_EXTENSION *dext)
  {
  int idx;
  ASN1_OBJECT *obj;
  obj = X509_EXTENSION_get_object(dext);
  while ((idx = X509v3_get_ext_by_OBJ(sk, obj, -1)) >= 0) {
  X509_EXTENSION *tmpext = X509v3_get_ext(sk, idx);
  X509v3_delete_ext(sk, idx);
  X509_EXTENSION_free(tmpext);
  }
  }
 **/
int OPENSSL_hexchar2int(unsigned char c)
{
#ifdef CHARSET_EBCDIC
	c = os_toebcdic[c];
#endif

	switch (c) {
		case '0':
			return 0;
		case '1':
			return 1;
		case '2':
			return 2;
		case '3':
			return 3;
		case '4':
			return 4;
		case '5':
			return 5;
		case '6':
			return 6;
		case '7':
			return 7;
		case '8':
			return 8;
		case '9':
			return 9;
		case 'a': case 'A':
			return 0x0A;
		case 'b': case 'B':
			return 0x0B;
		case 'c': case 'C':
			return 0x0C;
		case 'd': case 'D':
			return 0x0D;
		case 'e': case 'E':
			return 0x0E;
		case 'f': case 'F':
			return 0x0F;
	}
	return -1;
}

/*
 *  * Give a string of hex digits convert to a buffer
 *   */
unsigned char *OPENSSL_hexstr2buf(const char *str, long *len)
{
	unsigned char *hexbuf, *q; 
	unsigned char ch, cl; 
	int chi, cli;
	const unsigned char *p; 
	size_t s;

	s = strlen(str);
	if ((hexbuf = OPENSSL_malloc(s >> 1)) == NULL) {
		return NULL;
	}   
	for (p = (const unsigned char *)str, q = hexbuf; *p; ) { 
		ch = *p++;
		if (ch == ':')
			continue;
		cl = *p++;
		if (!cl) {
			OPENSSL_free(hexbuf);
			return NULL;
		}
		cli = OPENSSL_hexchar2int(cl);
		chi = OPENSSL_hexchar2int(ch);
		if (cli < 0 || chi < 0) {
			OPENSSL_free(hexbuf);
			return NULL;
		}
		*q++ = (unsigned char)((chi << 4) | cli);
	}   

	if (len)
		*len = q - hexbuf;
	return hexbuf;
}


/* Create a generic extension: for now just handle DER type */
static X509_EXTENSION *v3_generic_extension(const char *ext, const char *value, int crit, int gen_type, X509V3_CTX *ctx)
{
	unsigned char *ext_der = NULL;
	long ext_len = 0;
	ASN1_OBJECT *obj = NULL;
	ASN1_OCTET_STRING *oct = NULL;
	X509_EXTENSION *extension = NULL;

	if ((obj = OBJ_txt2obj(ext, 0)) == NULL) {
		goto err;
	}

	if (gen_type == 1)
		ext_der = OPENSSL_hexstr2buf(value, &ext_len);
	else if (gen_type == 2)
		ext_der = generic_asn1(value, ctx, &ext_len);

	if (ext_der == NULL) {
		goto err;
	}

	if ((oct = ASN1_OCTET_STRING_new()) == NULL) {
		goto err;
	}

	oct->data = ext_der;
	oct->length = ext_len;
	ext_der = NULL;

	extension = X509_EXTENSION_create_by_OBJ(NULL, obj, crit, oct);

err:
	ASN1_OBJECT_free(obj);
	ASN1_OCTET_STRING_free(oct);
	OPENSSL_free(ext_der);
	return extension;

}


stir_shaken_status_t stir_shaken_v3_add_ext(X509 *ca_x, X509 *x, X509_REQ *req, X509_CRL *crl, int nid, char *value)
{
	X509_EXTENSION *ex;
	X509V3_CTX ctx;

	if (!ca_x || !x) {
		return STIR_SHAKEN_STATUS_RESTART;
	}

	X509V3_set_ctx_nodb(&ctx);

	X509V3_set_ctx(&ctx, ca_x, x, req, crl, 0);
	ex = X509V3_EXT_conf_nid(NULL, &ctx, nid, value);
	if (!ex) {
		return STIR_SHAKEN_STATUS_TERM;
	}

	if (1 != X509_add_ext(x, ex, -1)) {
		return STIR_SHAKEN_STATUS_FALSE;
	}

	X509_EXTENSION_free(ex);
	return STIR_SHAKEN_STATUS_OK;
}

static int stir_shaken_v3_add_extensions(X509V3_CTX *ctx, const char *ext_name, const char *ext_value, X509_REQ *req, X509 *x)
{
	int                         i = 0, ext_type = -1, crit = 0;
	X509_EXTENSION              *ext = NULL;
	STACK_OF(X509_EXTENSION)    *extlist = NULL, **sk = NULL;
	uint8_t                     doing_req = 0, doing_x = 0;

	if (req) {
		sk = &extlist;
		doing_req = 1;
	}

	if (x) {
		doing_x = 1;
	}

	if ((doing_req ^ doing_x) == 0) return 0;

	if (doing_x) {

		// Handle adding of extensions for certificate generation
		crit = 0;   // TODO v3_check_critical(&ext_value);
		ext_type = 1;  // TODO this may fail at some future, should they change type definitions, can 
		if (!(ext = v3_generic_extension(ext_name, ext_value, crit, ext_type, NULL)))
			return 0;

		if (!ext) return 0;
		if (X509_add_ext(x, ext, -1) == 0) {
			X509_EXTENSION_free(ext);
			return 0;
		}

		X509_EXTENSION_free(ext);
		return 1;
	}

	if ((ext = X509V3_EXT_nconf(NULL, ctx, (char *)ext_name, (char*)ext_value)) == NULL)
		return 0;
	//if (ctx->flags == X509V3_CTX_REPLACE)
	//    delete_ext(*sk, ext);
	if (sk != NULL) {
		if (X509v3_add_ext(sk, ext, -1) == NULL) {
			X509_EXTENSION_free(ext);
			return 0;
		}
	}
	X509_EXTENSION_free(ext);
	i = X509_REQ_add_extensions(req, extlist);
	sk_X509_EXTENSION_pop_free(extlist, X509_EXTENSION_free);

	return i;
}

static int stir_shaken_generate_der(char *der, int der_len, uint32_t sp_code, int include_der_string)
{
	int             len = -1, tpl_len = -1, i = 0;
	// config is in form "SEQUENCE:tn_auth_list\n[tn_auth_list]\nfield1=EXP:0,IA5:1237";
	const char      *der_string = "DER:";
	const char      *der_template = "30:08:a0:06:16:04";   // TNAuthorizationList extension value, this is "field1=EXP:0,IA5:" in DER format
	char sp_code_str[100] = {0};

	len = snprintf(sp_code_str, 100, "%u", sp_code);
	if (len >= 100) {
		return -1;
	}

	tpl_len = snprintf(der, der_len, "%s%s", include_der_string ? der_string : "", der_template);
	if (tpl_len >= der_len) {
		return -1;
	}

	if (3*len + tpl_len + 1 > der_len) {
		return -1;
	}

	i = 0;
	len = strlen(sp_code_str);
	while (len) {
		der[tpl_len + 3*i] = ':';
		der[tpl_len + 3*i + 1] = '3';
		der[tpl_len + 3*i + 2] = sp_code_str[i];
		++i;
		len--;
	}
	der[tpl_len + 3*i] = '\0';

	return tpl_len + 3*i; // excluding '\0'
}

#define EXT_VALUE_DER_LEN 300
stir_shaken_status_t stir_shaken_generate_csr(stir_shaken_context_t *ss, uint32_t sp_code, X509_REQ **csr_req, EVP_PKEY *private_key, EVP_PKEY *public_key, const char *subject_c, const char *subject_cn)
{
	X509_REQ                *req = NULL;
	X509_NAME				*tmp = NULL;
	//const char              *req_subj = "/C=US/ST=VA/L=ItsHere/O=SignalWire, Inc./OU=VOIP/CN=SHAKEN";
	int                     req_multirdn = 0;
	unsigned long           req_chtype = 4097;
	X509V3_CTX              ext_ctx = {0};
	const EVP_MD            *digest = NULL;
	// our lovely extension configuration (input to DER) is:
	// asn1=SEQUENCE:tn_auth_list
	// [tn_auth_list]
	// field1=EXP:0,IA5:1237
	//const char *der_input = "SEQUENCE:tn_auth_list\n[tn_auth_list]\nfield1=EXP:0,IA5:1237";
	const char              *ext_name = "1.3.6.1.5.5.7.1.26";                   // our lovely TNAuthorizationList extension identifier that we will use to construct v3 extension of type TNAuthorizationList
	//const char              *ext_value = "DER:30:08:a0:06:16:04:37:38:36:35";   // TNAuthorizationList extension value, this is "asn1=SEQUENCE:tn_auth_list\ntn_auth_list]\nfield1=EXP:0,IA5:1237" in DER format
	char                    ext_value_der[EXT_VALUE_DER_LEN] = {0};
	int                     der_len = -1, include_der_string = 1;
	size_t                  i = 0;
	char					err_buf[STIR_SHAKEN_ERROR_BUF_LEN] = { 0 };
	
	
	stir_shaken_clear_error(ss);

	if (!sp_code || !csr_req) {
		stir_shaken_set_error(ss, "Generate CSR: Bad params", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}
	
	if (!subject_c) {
		stir_shaken_set_error(ss, "Subject 'C' for X509 CSR not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}
	
	if (!subject_cn) {
		stir_shaken_set_error(ss, "Subject 'CN' for X509 CSR not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	req = X509_REQ_new();
	if (!req) {
		stir_shaken_set_error(ss, "Generate CSR: SSL error while creating CSR", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

	// Make request (similar to OpenSSL's make_REQ from req.c

	if (!X509_REQ_set_version(req, 2L)) {
		stir_shaken_set_error(ss, "Failed to set version on CSR", STIR_SHAKEN_ERROR_SSL);
		goto fail;               /* version 1 */
	}
	
	tmp = X509_REQ_get_subject_name(req);
	if (!tmp) {
		stir_shaken_set_error(ss, "Failed to get X509_REQ subject name", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	if (!X509_NAME_add_entry_by_txt(tmp, "C", MBSTRING_ASC, subject_c, -1, -1, 0)) {
		stir_shaken_set_error(ss, "Failed to set X509_REQ subject 'C'", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	if (!X509_NAME_add_entry_by_txt(tmp,"CN", MBSTRING_ASC, subject_cn, -1, -1, 0)) {
		stir_shaken_set_error(ss, "Failed to set X509_REQ subject 'CN'", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	if (1 != X509_REQ_set_subject_name(req, tmp)) {
		stir_shaken_set_error(ss, "Failed to set X509_REQ subject name", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	if (!X509_REQ_set_pubkey(req, public_key)) {
		stir_shaken_set_error(ss, "Generate CSR: SSL error while setting EVP_KEY on CSR", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

	// TODO Factor out extensions and CSR signing (as did with cert)

	// Set up V3 context struct
	X509V3_set_ctx(&ext_ctx, NULL, NULL, req, NULL, 0);

	// DER
	include_der_string = 1;
	der_len = stir_shaken_generate_der(ext_value_der, EXT_VALUE_DER_LEN, sp_code, include_der_string);
	if (der_len == -1) {
		stir_shaken_set_error(ss, "Generate CSR: Failed to generate DER", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

	// Add extensions
	i = stir_shaken_v3_add_extensions(&ext_ctx, ext_name, ext_value_der, req, NULL);
	if (i == 0) {
		stir_shaken_set_error(ss, "Generate CSR: Cannot load extensions into CSR", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

	digest = EVP_get_digestbyname("sha256");
	if (!digest) {
		stir_shaken_set_error(ss, "Generate CSR: SSL error: Failed loading digest", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

	i = do_X509_REQ_sign(req, private_key, digest, NULL);
	if (i == 0) {
		stir_shaken_set_error(ss, "Generate CSR: SSL error: Failed to sign CSR", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

anyway:

	*csr_req = req;
	return STIR_SHAKEN_STATUS_OK;

fail:

	if (req) {
		X509_REQ_free(req);
		req = NULL;
	}
	
	stir_shaken_set_error_if_clear(ss, "Generate CSR: Error", STIR_SHAKEN_ERROR_SSL);
	return STIR_SHAKEN_STATUS_FALSE;
}

stir_shaken_status_t stir_shaken_csr_to_disk(stir_shaken_context_t *ss, X509_REQ *req, const char *csr_full_name, const char *csr_text_full_name)
{
	int i = 0;
	BIO *out = NULL, *bio = NULL;
	char err_buf[STIR_SHAKEN_ERROR_BUF_LEN] = { 0 };

	out = BIO_new(BIO_s_file());
	if (!out) {
		stir_shaken_set_error(ss, "Generate CSR: SSL error [1]", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	if (csr_full_name) {
		i = BIO_write_filename(out, (char*)csr_full_name);
		if (i == 0) {
			sprintf(err_buf, "Failed to redirect bio to file %s", csr_full_name);
			stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);
			goto fail;
		}

		i = PEM_write_bio_X509_REQ(out, req);
		if (i == 0) {
			sprintf(err_buf, "Error writing certificate to file %s", csr_full_name);
			stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);
			goto fail;
		}
	}

	BIO_free_all(out);
	out = NULL;

	if (csr_text_full_name) {
		bio = BIO_new_file(csr_text_full_name, "w");
		if (!bio) {
			stir_shaken_set_error(ss, "Generate CSR: SSL error [2]", STIR_SHAKEN_ERROR_SSL);
			goto anyway;
		}

		X509_REQ_print_ex(bio, req, 0, 0);
		BIO_free_all(bio);
		bio = NULL;
	}

anyway:
	return STIR_SHAKEN_STATUS_OK;

fail:

	if (out) {
		BIO_free_all(out);
		out = NULL;
	}

	if (bio) {
		BIO_free_all(bio);
		bio = NULL;
	}

	stir_shaken_set_error_if_clear(ss, "Error", STIR_SHAKEN_ERROR_SSL);
	return STIR_SHAKEN_STATUS_FALSE;
}

void stir_shaken_destroy_csr(X509_REQ **csr_req)
{
	if (csr_req) {
	
		if (*csr_req) {
		
			X509_REQ_free(*csr_req);
		}

		*csr_req = NULL;
	}
}

// Create new X509 certificate
X509* stir_shaken_generate_x509_cert(stir_shaken_context_t *ss, EVP_PKEY *public_key, const char* issuer_c, const char *issuer_cn, const char *subject_c, const char *subject_cn, int serial, int expiry_days)
{
	X509 *x = NULL;
	X509_NAME		*tmp = NULL;
	const EVP_MD    *digest = NULL;
	int             i = 0;
	
	
	stir_shaken_clear_error(ss);

	
	if (!subject_c) {
		// Just a warning
		stir_shaken_set_error(ss, "Subject's 'C' for X509 not set", STIR_SHAKEN_ERROR_GENERAL);
	}
	
	if (!subject_cn) {
		// Just a warning
		stir_shaken_set_error(ss, "Subject 'CN' for X509 not set", STIR_SHAKEN_ERROR_GENERAL);
	}

	if (!issuer_c) {
		stir_shaken_set_error(ss, "Issuer 'C' for X509 not set", STIR_SHAKEN_ERROR_GENERAL);
		return NULL;
	}
	
	if (!issuer_cn) {
		stir_shaken_set_error(ss, "Issuer 'CN' for X509 not set", STIR_SHAKEN_ERROR_GENERAL);
		return NULL;
	}

	if ((x = X509_new()) == NULL) {
		stir_shaken_set_error(ss, "SSL error while creating new X509 certificate", STIR_SHAKEN_ERROR_SSL);
		return NULL;
	}

	if (!X509_set_version(x, 2L)) {
		stir_shaken_set_error(ss, "Failed to set version on Certificate", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}
	
	if (1 != X509_set_pubkey(x, public_key)) {
		stir_shaken_set_error(ss, "Failed to set public key on the Certificate", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

	ASN1_INTEGER_set(X509_get_serialNumber(x), serial);

	tmp = X509_get_issuer_name(x);
	if (!tmp) {
		stir_shaken_set_error(ss, "Failed to get X509 issuer name", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

	if (!X509_NAME_add_entry_by_txt(tmp, "C", MBSTRING_ASC, issuer_c, -1, -1, 0)) {
		stir_shaken_set_error(ss, "Failed to set X509 issuer 'C'", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

	if (!X509_NAME_add_entry_by_txt(tmp,"CN", MBSTRING_ASC, issuer_cn, -1, -1, 0)) {
		stir_shaken_set_error(ss, "Failed to set X509 issuer 'CN'", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

	if (1 != X509_set_issuer_name(x, tmp)) {
		stir_shaken_set_error(ss, "Failed to set X509 issuer name", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

	if (subject_c || subject_cn) {

		if (!subject_c && subject_cn) {
			stir_shaken_set_error(ss, "When setting subject name for X509 certifiate both 'C' and 'CN' must be set", STIR_SHAKEN_ERROR_SSL);
			goto fail;
		}

		tmp = X509_get_subject_name(x);
		if (!tmp) {
			stir_shaken_set_error(ss, "Failed to get X509 subject name", STIR_SHAKEN_ERROR_SSL);
			goto fail;
		}

		if (!X509_NAME_add_entry_by_txt(tmp, "C", MBSTRING_ASC, subject_c, -1, -1, 0)) {
			stir_shaken_set_error(ss, "Failed to set X509 subject 'C'", STIR_SHAKEN_ERROR_SSL);
			goto fail;
		}

		if (!X509_NAME_add_entry_by_txt(tmp,"CN", MBSTRING_ASC, subject_cn, -1, -1, 0)) {
			stir_shaken_set_error(ss, "Failed to set X509 subject 'CN'", STIR_SHAKEN_ERROR_SSL);
			goto fail;
		}

		if (1 != X509_set_subject_name(x, tmp)) {
			stir_shaken_set_error(ss, "Failed to set X509 subject name", STIR_SHAKEN_ERROR_SSL);
			goto fail;
		}
	}

	X509_gmtime_adj(X509_get_notBefore(x), 0);
	X509_gmtime_adj(X509_get_notAfter(x), expiry_days * 24 * 60 * 60);

	return x;

fail:
	if (x) {
		X509_free(x);
		x = NULL;
	}
	stir_shaken_set_error_if_clear(ss, "Error creating cert", STIR_SHAKEN_ERROR_GENERAL);
	return NULL;
}

stir_shaken_status_t stir_shaken_sign_x509_cert(stir_shaken_context_t *ss, X509 *x, EVP_PKEY *private_key)
{
	const EVP_MD    *md = NULL;
	const char		*digest_name = "sha256";
	char err_buf[STIR_SHAKEN_ERROR_BUF_LEN] = { 0 };

	stir_shaken_clear_error(ss);

	if (!x) {
		stir_shaken_set_error(ss, "X509 cert not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}
	
	if (!private_key) {
		stir_shaken_set_error(ss, "Private key not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	md = EVP_get_digestbyname(digest_name);
	if (!md) {
		sprintf(err_buf, "Cannot get %s digest", digest_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	if (!X509_sign(x, private_key, md)) {
		stir_shaken_set_error(ss, "Failed to sign certificate", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	return STIR_SHAKEN_STATUS_OK;
}

stir_shaken_status_t stir_shaken_x509_add_standard_extensions(stir_shaken_context_t *ss, X509 *ca_x, X509 *x)
{
	if (!x) {
		stir_shaken_set_error(ss, "Subject certificate not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}
	
	if (!ca_x) {
		stir_shaken_set_error(ss, "CA certificate not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}
	
	// Subject Key Identifier extension
	// TODO pass CRL and/or REQ
	if (STIR_SHAKEN_STATUS_OK != stir_shaken_v3_add_ext(ca_x, x, NULL, NULL, NID_subject_key_identifier, "hash")) {
		stir_shaken_set_error(ss, "Failed to add Subject Key Identifier v3 extension to X509", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	// Authority Key Identifier extension
	// TODO pass CRL and/or REQ
	if (STIR_SHAKEN_STATUS_OK != stir_shaken_v3_add_ext(ca_x, x, NULL, NULL, NID_authority_key_identifier, "keyid:always")) {
		stir_shaken_set_error(ss, "Failed to add Authority Key Identifier v3 extension to X509", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	// Comment
	// TODO pass CRL and/or REQ
	if (STIR_SHAKEN_STATUS_OK != stir_shaken_v3_add_ext(ca_x, x, NULL, NULL, NID_netscape_comment, "Always look on the bright side of life")) {
		stir_shaken_set_error(ss, "Failed to add custom v3 extension to X509", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	return STIR_SHAKEN_STATUS_OK;
}

// The keyIdentifier field of the authorityKeyIdentifier extension MUST be included
// in all certificates generated by conforming CAs to facilitate certification path construction.
stir_shaken_status_t stir_shaken_x509_add_ca_extensions(stir_shaken_context_t *ss, X509 *ca_x, X509 *x)
{
	if (!x) {
		stir_shaken_set_error(ss, "Subject certificate not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}
	
	if (!ca_x) {
		stir_shaken_set_error(ss, "CA certificate not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}
	
	// TODO pass CRL and/or REQ
	if (STIR_SHAKEN_STATUS_OK != stir_shaken_v3_add_ext(ca_x, x, NULL, NULL, NID_basic_constraints, "critical,CA:TRUE")) {
		stir_shaken_set_error(ss, "Failed to add Subject Key Identifier v3 extension to X509", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	// TODO pass CRL and/or REQ
	if (STIR_SHAKEN_STATUS_OK != stir_shaken_v3_add_ext(ca_x, x, NULL, NULL, NID_key_usage, "critical,keyCertSign,cRLSign")) {
		stir_shaken_set_error(ss, "Failed to add Subject Key Identifier v3 extension to X509", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	// TODO pass CRL and/or REQ
	if (STIR_SHAKEN_STATUS_OK != stir_shaken_v3_add_ext(ca_x, x, NULL, NULL, NID_netscape_cert_type, "sslCA")) {
		stir_shaken_set_error(ss, "Failed to add Subject Key Identifier v3 extension to X509", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	return STIR_SHAKEN_STATUS_OK;
}

stir_shaken_status_t stir_shaken_x509_add_signalwire_extensions(stir_shaken_context_t *ss, X509 *ca_x, X509 *x, const char *number_start, const char *number_end)
{
	if (!x) {
		stir_shaken_set_error(ss, "Subject certificate not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}
	
	if (!ca_x) {
		stir_shaken_set_error(ss, "CA certificate not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}

#if STIR_SHAKEN_CERT_ADD_SIGNALWIRE_EXTENSION
	{
		char ext_value[400] = { 0 };
		int nid;
		
		if (!number_start || !number_end) {
			stir_shaken_set_error(ss, "Number range not set", STIR_SHAKEN_ERROR_GENERAL);
			return STIR_SHAKEN_STATUS_TERM;
		}

		nid = OBJ_create("1.2.3.4.5.6.7.8.9", "SignalWire STIR-Shaken", "SignalWire STIR-Shaken");
		X509V3_EXT_add_alias(nid, NID_netscape_comment);
		
		sprintf(ext_value, "Number range: %s - %s", number_start, number_end);

		if (STIR_SHAKEN_STATUS_OK != stir_shaken_v3_add_ext(ca_x, x, NULL, NULL, nid, ext_value)) {
			stir_shaken_set_error(ss, "Failed to add SignalWire's v3 extension to X509", STIR_SHAKEN_ERROR_SSL);
			return STIR_SHAKEN_STATUS_TERM;
		}
	}
#endif

	return STIR_SHAKEN_STATUS_OK;
}

// Create CA cross-certificate, where issuer and subject are different entities.
// Cross certificates describe a trust relationship between CAs.
X509* stir_shaken_generate_x509_cross_ca_cert(stir_shaken_context_t *ss, X509 *ca_x, EVP_PKEY *private_key, EVP_PKEY *public_key, const char* issuer_c, const char *issuer_cn, const char *subject_c, const char *subject_cn, int serial, int expiry_days)
{
	X509 *x = NULL;
	
	x = stir_shaken_generate_x509_cert(ss, public_key, issuer_c, issuer_cn, subject_c, subject_cn, serial, expiry_days);
	if (!x) {
		stir_shaken_set_error_if_clear(ss, "Failed to generate initial X509 certificate", STIR_SHAKEN_ERROR_GENERAL);
		return NULL;
	}
	
	if (STIR_SHAKEN_STATUS_OK != stir_shaken_x509_add_standard_extensions(ss, ca_x, x)) {
		stir_shaken_set_error_if_clear(ss, "Failed to add standard X509 extensions", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}

	if (STIR_SHAKEN_STATUS_OK != stir_shaken_x509_add_ca_extensions(ss, x, x)) {
		stir_shaken_set_error_if_clear(ss, "Failed to add CA extensions to X509 cross certificate", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}

	if (STIR_SHAKEN_STATUS_OK != stir_shaken_sign_x509_cert(ss, x, private_key)) {
		stir_shaken_set_error_if_clear(ss, "Failed to sign X509 CA cross certificate", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}
	
	// Subject and issuer must be different entities
	if (!strcmp(issuer_c, subject_c) && !strcmp(issuer_cn, subject_cn)) {
		stir_shaken_set_error(ss, "Issuer and subject entities must be different for X509 CA cross certificate", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}

	return x;

fail:
	if (x) X509_free(x);
	stir_shaken_set_error_if_clear(ss, "Failed to generate X509 end entity certificate", STIR_SHAKEN_ERROR_GENERAL);
	return NULL;
}

// Create CA self-issued certificate, where issuer and the subject are same entity.
// Self-issued certs describe a change in policy or operation.
X509* stir_shaken_generate_x509_self_issued_ca_cert(stir_shaken_context_t *ss, EVP_PKEY *private_key, EVP_PKEY *public_key, const char* issuer_c, const char *issuer_cn, int serial, int expiry_days)
{
	X509 *x = NULL;

	x = stir_shaken_generate_x509_cert(ss, public_key, issuer_c, issuer_cn, issuer_c, issuer_cn, serial, expiry_days);
	if (!x) {
		stir_shaken_set_error_if_clear(ss, "Failed to generate initial X509 certificate", STIR_SHAKEN_ERROR_GENERAL);
		return NULL;
	}

	if (STIR_SHAKEN_STATUS_OK != stir_shaken_x509_add_standard_extensions(ss, x, x)) {
		stir_shaken_set_error_if_clear(ss, "Failed to add standard X509 extensions", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}

	if (STIR_SHAKEN_STATUS_OK != stir_shaken_x509_add_ca_extensions(ss, x, x)) {
		stir_shaken_set_error_if_clear(ss, "Failed to add CA extensions to X509 self-issued certificate", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}

	if (STIR_SHAKEN_STATUS_OK != stir_shaken_sign_x509_cert(ss, x, private_key)) {
		stir_shaken_set_error_if_clear(ss, "Failed to sign X509 self-issued CA certificate", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}

	return x;

fail:
	if (x) X509_free(x);
	stir_shaken_set_error_if_clear(ss, "Failed to generate X509 self-issued CA certificate", STIR_SHAKEN_ERROR_GENERAL);
	return NULL;
}

// Create CA self-signed certificate, which is self-issued certificate
// where the digital signature may be verified by the public key bound into the certificate.
X509* stir_shaken_generate_x509_self_signed_ca_cert(stir_shaken_context_t *ss, EVP_PKEY *private_key, EVP_PKEY *public_key, const char* issuer_c, const char *issuer_cn, int serial, int expiry_days)
{
	X509 *x = NULL;

	// Self signed certificate is a special case of self-issued certificate,
	// with a property that it's digital signature may be verified by the public key bound into the certificate.
	
	// TODO check signature, private/public key pair match
	
	return stir_shaken_generate_x509_self_issued_ca_cert(ss, private_key, public_key, issuer_c, issuer_cn, serial, expiry_days);
}

// Create SP certificate.
X509* stir_shaken_generate_x509_end_entity_cert(stir_shaken_context_t *ss, X509 *ca_x, EVP_PKEY *private_key, EVP_PKEY *public_key, const char* issuer_c, const char *issuer_cn, const char *subject_c, const char *subject_cn, int serial, int expiry_days, const char *number_start, const char *number_end)
{
	X509 *x = NULL;
	
	// Subject and issuer must be different entities
	if (!strcmp(issuer_c, subject_c) && !strcmp(issuer_cn, subject_cn)) {
		stir_shaken_set_error(ss, "Issuer and subject entities must be different for an end entity X509 certificate", STIR_SHAKEN_ERROR_GENERAL);
		return NULL;
	}

	x = stir_shaken_generate_x509_cert(ss, public_key, issuer_c, issuer_cn, subject_c, subject_cn, serial, expiry_days);
	if (!x) {
		stir_shaken_set_error_if_clear(ss, "Failed to generate initial X509 certificate", STIR_SHAKEN_ERROR_GENERAL);
		return NULL;
	}
	
	if (STIR_SHAKEN_STATUS_OK != stir_shaken_x509_add_standard_extensions(ss, ca_x, x)) {
		stir_shaken_set_error_if_clear(ss, "Failed to add standard X509 extensions", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}

	if (STIR_SHAKEN_STATUS_OK != stir_shaken_x509_add_signalwire_extensions(ss, ca_x, x, number_start, number_end)) {
		stir_shaken_set_error_if_clear(ss, "Failed to add SignalWire's X509 extensions", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}
	
	if (STIR_SHAKEN_STATUS_OK != stir_shaken_sign_x509_cert(ss, x, private_key)) {
		stir_shaken_set_error_if_clear(ss, "Failed to sign X509 end entity certificate", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}
	
	return x;

fail:
	if (x) X509_free(x);
	stir_shaken_set_error_if_clear(ss, "Failed to generate X509 end entity certificate", STIR_SHAKEN_ERROR_GENERAL);
	return NULL;
}

// Create SP certificate from CSR.
X509* stir_shaken_generate_x509_end_entity_cert_from_csr(stir_shaken_context_t *ss, X509 *ca_x, EVP_PKEY *private_key, const char* issuer_c, const char *issuer_cn, X509_REQ *req, int serial, int expiry_days, const char *number_start, const char *number_end)
{
	X509 *x = NULL;
	EVP_PKEY *public_key = NULL;

	if (!req) {
		stir_shaken_set_error(ss, "X509 req not set", STIR_SHAKEN_ERROR_GENERAL);
		return NULL;
	}

	if (!issuer_c || !issuer_cn) {
		stir_shaken_set_error(ss, "Issuer not set", STIR_SHAKEN_ERROR_GENERAL);
		return NULL;
	}

	public_key = X509_REQ_get_pubkey(req);
	if (!public_key) {
		stir_shaken_set_error(ss, "No public key in X509 certificate", STIR_SHAKEN_ERROR_SSL);
		return NULL;
	}

	x = stir_shaken_generate_x509_cert(ss, public_key, issuer_c, issuer_cn, NULL, NULL, serial, expiry_days);
	if (!x) {
		stir_shaken_set_error_if_clear(ss, "Failed to generate initial X509 certificate", STIR_SHAKEN_ERROR_GENERAL);
		return NULL;
	}

	X509_set_subject_name(x, X509_REQ_get_subject_name(req));
	
	if (STIR_SHAKEN_STATUS_OK != stir_shaken_x509_add_standard_extensions(ss, ca_x, x)) {
		stir_shaken_set_error_if_clear(ss, "Failed to add standard X509 extensions", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}

	if (STIR_SHAKEN_STATUS_OK != stir_shaken_x509_add_signalwire_extensions(ss, ca_x, x, number_start, number_end)) {
		stir_shaken_set_error_if_clear(ss, "Failed to add SignalWire's X509 extensions", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}
	
	if (STIR_SHAKEN_STATUS_OK != stir_shaken_sign_x509_cert(ss, x, private_key)) {
		stir_shaken_set_error_if_clear(ss, "Failed to sign X509 end entity certificate", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}
	
	return x;

fail:
	if (x) X509_free(x);
	stir_shaken_set_error_if_clear(ss, "Failed to generate X509 end entity certificate", STIR_SHAKEN_ERROR_GENERAL);
	return NULL;
}

stir_shaken_status_t stir_shaken_add_tnauthlist_extension(stir_shaken_context_t *ss, uint32_t sp_code, X509 *x)
{
	int             i = 0;
	const char      *ext_name = "1.3.6.1.5.5.7.1.26";                   // our lovely TNAuthorizationList extension identifier that we will use to construct v3 extension of type TNAuthorizationList
	char            ext_value_der[EXT_VALUE_DER_LEN] = {0};
	int             der_len = -1, include_der_string = 0;

	if (!x) {
		stir_shaken_set_error(ss, "CSR not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	include_der_string = 0;
	der_len = stir_shaken_generate_der(ext_value_der, EXT_VALUE_DER_LEN, sp_code, include_der_string);
	if (der_len == -1) {
		stir_shaken_set_error(ss, "Failed to generate DER", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	i = stir_shaken_v3_add_extensions(NULL, ext_name, ext_value_der, NULL, x);
	if (i == 0) {
		stir_shaken_set_error(ss, "Cannot load extensions into CSR", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}
	
	return STIR_SHAKEN_STATUS_OK;
}

X509* stir_shaken_generate_x509_cert_from_csr(stir_shaken_context_t *ss, uint32_t sp_code, X509_REQ *req, EVP_PKEY *private_key, const char* issuer_c, const char *issuer_cn, int serial, int expiry_days)
{
	X509            *x = NULL;
	EVP_PKEY        *pkey = NULL;
	X509_NAME		*tmp = NULL;
	int             i = 0;
	const char      *ext_name = "1.3.6.1.5.5.7.1.26";                   // our lovely TNAuthorizationList extension identifier that we will use to construct v3 extension of type TNAuthorizationList
	char            ext_value_der[EXT_VALUE_DER_LEN] = {0};
	int             der_len = -1, include_der_string = 0;
	
	
	stir_shaken_clear_error(ss);

	if (!req) {
		stir_shaken_set_error(ss, "X509 CSR not set", STIR_SHAKEN_ERROR_GENERAL);
		return NULL;
	}
	
	if (!issuer_c) {
		stir_shaken_set_error(ss, "Issuer 'C' for X509 not set", STIR_SHAKEN_ERROR_GENERAL);
		return NULL;
	}
	
	if (!issuer_cn) {
		stir_shaken_set_error(ss, "Issuer 'CN' for X509 not set", STIR_SHAKEN_ERROR_GENERAL);
		return NULL;
	}

	// Check csr
	if (!(pkey = X509_REQ_get_pubkey(req))) {
		stir_shaken_set_error(ss, "Cannot get public key from X509_REQ", STIR_SHAKEN_ERROR_SSL);
		return NULL;
	}

	i = X509_REQ_verify(req, pkey);
	if (i < 0) {
		stir_shaken_set_error(ss, "'X509_REQ-public key' pair invalid", STIR_SHAKEN_ERROR_SSL);
		return NULL;
	}

	EVP_PKEY_free(pkey);
	pkey = NULL;

	if ((x = X509_new()) == NULL) {
		stir_shaken_set_error(ss, "SSL error while creating new X509 certificate", STIR_SHAKEN_ERROR_SSL);
		return NULL;
	}

	if (!X509_set_version(x, 2L)) {
		stir_shaken_set_error(ss, "Failed to set version on Certificate", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

	ASN1_INTEGER_set(X509_get_serialNumber(x), serial);

	tmp = X509_get_issuer_name(x);
	if (!tmp) {
		stir_shaken_set_error(ss, "Failed to get X509 issuer name", STIR_SHAKEN_ERROR_SSL);
		return NULL;
	}

	if (!X509_NAME_add_entry_by_txt(tmp, "C", MBSTRING_ASC, issuer_c, -1, -1, 0)) {
		stir_shaken_set_error(ss, "Failed to set X509 issuer 'C'", STIR_SHAKEN_ERROR_SSL);
		return NULL;
	}

	if (!X509_NAME_add_entry_by_txt(tmp,"CN", MBSTRING_ASC, issuer_cn, -1, -1, 0)) {
		stir_shaken_set_error(ss, "Failed to set X509 issuer 'CN'", STIR_SHAKEN_ERROR_SSL);
		return NULL;
	}

	if (1 != X509_set_issuer_name(x, tmp)) {
		stir_shaken_set_error(ss, "Failed to set X509 issuer name", STIR_SHAKEN_ERROR_SSL);
		return NULL;
	}

	X509_set_subject_name(x, X509_REQ_get_subject_name(req));

	X509_gmtime_adj(X509_get_notBefore(x), 0);
	X509_time_adj_ex(X509_get_notAfter(x), expiry_days * 24 * 60 * 60, 0, NULL);

	pkey = X509_REQ_get_pubkey(req);
	X509_set_pubkey(x, pkey);
	EVP_PKEY_free(pkey);
	pkey = NULL;

	return x;

fail:
	if (x) {
		X509_free(x);
		x = NULL;
	}
	stir_shaken_set_error_if_clear(ss, "Error creating cert", STIR_SHAKEN_ERROR_SSL);
	return NULL;
}

#define DATE_LEN 128

static int stir_shaken_convert_ASN1TIME(stir_shaken_context_t *ss, ASN1_TIME *t, char* buf, size_t len)
{
	int	rc = -1;
	BIO	*b = BIO_new(BIO_s_mem());

	rc = ASN1_TIME_print(b, t);
	if (rc <= 0) {

		stir_shaken_set_error(ss, "ASN1_TIME_print failed or wrote no data", STIR_SHAKEN_ERROR_GENERAL);
		BIO_free(b);
		return EXIT_FAILURE;
	}

	rc = BIO_gets(b, buf, len);
	if (rc <= 0) {

		stir_shaken_set_error(ss, "BIO_gets call failed to transfer contents to buf", STIR_SHAKEN_ERROR_GENERAL);
		BIO_free(b);
		return EXIT_FAILURE;
	}

	BIO_free(b);
	return EXIT_SUCCESS;
}

stir_shaken_status_t stir_shaken_read_cert_fields(stir_shaken_context_t *ss, stir_shaken_cert_t *cert)
{
	X509			*x = NULL;
	ASN1_INTEGER	*serial = NULL;
	BIGNUM			*bnser = NULL;
	char			*serialHex = NULL;
	char			*serialDec = NULL;
	char			*issuer = NULL;
	char			*subject = NULL;
	char			not_before_str[ASN1_DATE_LEN] = { 0 };
	char			not_after_str[ASN1_DATE_LEN] = { 0 };
	ASN1_TIME		*notBefore = NULL;
	ASN1_TIME		*notAfter = NULL;
	int				version = -1;

	stir_shaken_clear_error(ss);

	if (!cert) {

		stir_shaken_set_error(ss, "Cert not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	x = cert->x;
	if (!x) {

		stir_shaken_set_error(ss, "Cert has no X509 struct", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	stir_shaken_destroy_cert_fields(cert);
	
	serial = X509_get_serialNumber(x);
	bnser = ASN1_INTEGER_to_BN(serial, NULL);

	serialHex = BN_bn2hex(bnser);
	cert->serialHex = serialHex;

	serialDec = BN_bn2dec(bnser);
	cert->serialDec = serialDec;

	X509_NAME_oneline(X509_get_issuer_name(x), cert->issuer, sizeof(cert->issuer));
	X509_NAME_oneline(X509_get_subject_name(x), cert->subject, sizeof(cert->subject));

	notBefore = X509_get_notBefore(x);
	cert->notBefore_ASN1 = notBefore;

	notAfter = X509_get_notAfter(x);
	cert->notAfter_ASN1 = notAfter;

	stir_shaken_convert_ASN1TIME(ss, notBefore, cert->notBefore, ASN1_DATE_LEN);
	stir_shaken_convert_ASN1TIME(ss, notAfter, cert->notAfter, ASN1_DATE_LEN);

	version = ((int) X509_get_version(x)) + 1;
	cert->version = version;

	return STIR_SHAKEN_STATUS_OK;
}

void stir_shaken_destroy_cert_fields(stir_shaken_cert_t *cert)
{
	if (cert) {

		if (cert->serialHex) {
			OPENSSL_free(cert->serialHex);
			cert->serialHex = NULL;
		}

		if (cert->serialDec) {
			OPENSSL_free(cert->serialDec);
			cert->serialDec = NULL;
		}

		memset(cert->issuer, 0, sizeof(cert->issuer));
		memset(cert->subject, 0, sizeof(cert->subject));

		if (cert->notBefore_ASN1) {
			//ASN1_TIME_free(cert->notBefore_ASN1); SSL returns internal pointers from cert to this, so DO NOT FREE this
			cert->notBefore_ASN1 = NULL;
		}

		if (cert->notAfter_ASN1) {
			//ASN1_TIME_free(cert->notAfter_ASN1); SSL returns internal pointers from cert to this, so DO NOT FREE this
			cert->notAfter_ASN1 = NULL;
		}
	}
}

static int stir_shaken_verify_callback(int ok, X509_STORE_CTX *ctx)
{
	X509 *err_cert = NULL;
    int err = 0, depth = 0;
	FILE *file = NULL;

    err_cert = X509_STORE_CTX_get_current_cert(ctx);
    err =   X509_STORE_CTX_get_error(ctx);
    depth = X509_STORE_CTX_get_error_depth(ctx);
	file = (FILE *) X509_STORE_CTX_get_ex_data(ctx, 0);

    fprintf(file, "===[depth: %d] X509 cert path validation: got error: %d ===\n", depth, err);

	if (err_cert) {
		
		stir_shaken_cert_t cert = { .x = err_cert };
		
		if (STIR_SHAKEN_STATUS_OK != stir_shaken_read_cert_fields(NULL, &cert)) {
			fprintf(file, "===[depth: %d] Bad, bad, bad\n", depth);
			goto handle_error;
		}

		fprintf(file, "===[depth: %d] = Certificate under consideration:\n", depth);
		stir_shaken_print_cert_fields(file, &cert);

    } else {

		fprintf(file, "===[depth: %d] = No cert for this error:\n", depth);
	}

handle_error:

    if (!ok) {

		fprintf(file,"===[depth: %d] Error detail: %d: %s\n", depth, err, X509_verify_cert_error_string(err));
	}

	switch (err) {

		case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
			fprintf(file, "===[depth: %d] E: Issuer\n", depth);
			break;

		case X509_V_ERR_CERT_NOT_YET_VALID:
		case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
			fprintf(file, "===[depth: %d] E: Cert not valid yet\n", depth);
			break;

		case X509_V_ERR_CERT_HAS_EXPIRED:
		case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
			fprintf(file, "===[depth: %d] E: Cert expired\n", depth);
			break;

		case X509_V_ERR_NO_EXPLICIT_POLICY:
			fprintf(file, "===[depth: %d] E: No explicit policy\n", depth);
			break;

		case X509_V_ERR_UNABLE_TO_GET_CRL:
			fprintf(file, "===[depth: %d] E: No explicit policy\n", depth);
			break;

		case X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE:
			fprintf(file, "===[depth: %d] E: Failed to decrypt CERT signature\n", depth);
			break;

		case X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE:
			fprintf(file, "===[depth: %d] E: Failed to decrypt CRL signature\n", depth);
			break;

		case X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY:
			fprintf(file, "===[depth: %d] E: Unable to decode issuer's public key\n", depth);
			break;

		case X509_V_ERR_CERT_SIGNATURE_FAILURE:
			fprintf(file, "===[depth: %d] E: Cert signature failure\n", depth);
			break;

		case X509_V_ERR_CRL_SIGNATURE_FAILURE:
			fprintf(file, "===[depth: %d] E: Crl signature failure\n", depth);
			break;

		case X509_V_ERR_CRL_NOT_YET_VALID:
			fprintf(file, "===[depth: %d] E: CRL not valid yet\n", depth);
			break;

		case X509_V_ERR_CRL_HAS_EXPIRED:
			fprintf(file, "===[depth: %d] E: CRL expired\n", depth);
			break;

		case X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD:
			fprintf(file, "===[depth: %d] E: CRL last update invalid\n", depth);
			break;

		case X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD:
			fprintf(file, "===[depth: %d] E: CRL next update invalid\n", depth);
			break;

		case X509_V_ERR_OUT_OF_MEM:
			fprintf(file, "===[depth: %d] E: Out of mem\n", depth);
			break;

		case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
			fprintf(file, "===[depth: %d] E: Self signed cert on depth 0 not allowed\n", depth);
			break;

		case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
			fprintf(file, "===[depth: %d] E: Self signed cert in chain not allowed\n", depth);
			break;

		case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
			fprintf(file, "===[depth: %d] E: Unable to get issuer's cert locally\n", depth);
			break;

		case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
			fprintf(file, "===[depth: %d] E: Unable to verify leaf signature\n", depth);
			break;

		case X509_V_ERR_CERT_CHAIN_TOO_LONG:
			fprintf(file, "===[depth: %d] E: Cert chain is too long\n", depth);
			break;

		case X509_V_ERR_CERT_REVOKED:
			fprintf(file, "===[depth: %d] E: Cert is revoked\n", depth);
			break;

		case X509_V_ERR_APPLICATION_VERIFICATION:
			fprintf(file, "===[depth: %d] E: Application verification error\n", depth);
			break;

		default:
			fprintf(file, "===[depth: %d] Default error\n", depth);
			break;
	}

    if (err == X509_V_OK && ok == 2) {
        
		/* print out policies */
		fprintf(file, "===[depth: %d] +++ Policy checking is complete\n", depth);
	}

	fprintf(file, "===[depth: %d] Translating error to: %d\n", depth, ok);
    return(ok);

}

unsigned long stir_shaken_get_cert_name_hashed(stir_shaken_context_t *ss, X509 *x)
{
	if (!x) {
		stir_shaken_set_error(ss, "X509 certificate not set", STIR_SHAKEN_ERROR_SSL);
		return 0;
	}

	return X509_NAME_hash(X509_get_subject_name(x));
}

void stir_shaken_cert_name_hashed_2_string(unsigned long hash, char *buf, int buflen)
{
	if (!buf) return;
	snprintf(buf, buflen, "%8lx", hash);
}

stir_shaken_status_t stir_shaken_init_cert_store(stir_shaken_context_t *ss, const char *ca_list, const char *ca_dir, const char *crl_list, const char *crl_dir)
{
	stir_shaken_globals_t *g = &stir_shaken_globals;

	if (g->store) {
		X509_STORE_free(g->store);
		g->store = NULL;
	}

	g->store = X509_STORE_new();
	if (!g->store) {
		stir_shaken_set_error(ss, "Failed to create X509_STORE", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	X509_STORE_set_verify_cb_func(g->store, stir_shaken_verify_callback);

	if (ca_list || ca_dir) {

		if (X509_STORE_load_locations(g->store, ca_list, ca_dir) != 1) {
			stir_shaken_set_error(ss, "Failed to load list of trusted CAs", STIR_SHAKEN_ERROR_LOAD_CA);
			goto fail;
		}
		
		if (STIR_SHAKEN_LOAD_CA_FROM_DEFAULT_OS_PATHS && (X509_STORE_set_default_paths(g->store) != 1)) {
			stir_shaken_set_error(ss, "Failed to load the system-wide CA certificates", STIR_SHAKEN_ERROR_SET_DEFAULT_PATHS);
			goto fail;
		}
	}

	if (crl_list || crl_dir) {

		if (X509_STORE_load_locations(g->store, crl_list, crl_dir) != 1) {
			stir_shaken_set_error(ss, "Failed to load CRLs", STIR_SHAKEN_ERROR_LOAD_CRL);
			goto fail;
		}

		// TODO Probably?
		X509_STORE_set_flags(g->store, X509_V_FLAG_CRL_CHECK | X509_V_FLAG_CRL_CHECK_ALL);
	}

	return STIR_SHAKEN_STATUS_OK;

fail:
	if (g->store) {
		X509_STORE_free(g->store);
		g->store = NULL;
	}
	return STIR_SHAKEN_STATUS_FALSE;
}

void stir_shaken_cert_store_cleanup(void)
{
	stir_shaken_globals_t *g = &stir_shaken_globals; 

	if (g->store) {
		X509_STORE_free(g->store);
		g->store = NULL;
	}
}

stir_shaken_status_t stir_shaken_verify_cert_path(stir_shaken_context_t *ss, stir_shaken_cert_t *cert)
{
	X509            *x = NULL;
	stir_shaken_globals_t *g = &stir_shaken_globals; 
	int rc = 1;
	char err_buf[STIR_SHAKEN_ERROR_BUF_LEN] = { 0 };
	int verify_error = -1;
	FILE *file = stderr;

	stir_shaken_clear_error(ss);

	if (!cert) {
		stir_shaken_set_error(ss, "Cert not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	if (!g->store) {
		stir_shaken_set_error(ss, "Cert store not set", STIR_SHAKEN_ERROR_CERT_STORE);
		return STIR_SHAKEN_STATUS_TERM;
	}

	if (cert->verify_ctx) {
		X509_STORE_CTX_cleanup(cert->verify_ctx);
		X509_STORE_CTX_free(cert->verify_ctx);
		cert->verify_ctx = NULL;
	}

	if (!(cert->verify_ctx = X509_STORE_CTX_new())) {
		stir_shaken_set_error(ss, "Failed to create X509_STORE_CTX object", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	pthread_mutex_lock(&g->mutex); // Can use X509_STORE_lock(g->store) for more fine grained locking later

	if (X509_STORE_CTX_init(cert->verify_ctx, g->store, cert->x, cert->xchain) != 1) {

		X509_STORE_CTX_cleanup(cert->verify_ctx);
		X509_STORE_CTX_free(cert->verify_ctx);
		cert->verify_ctx = NULL;
		stir_shaken_set_error(ss, "SSL: Error initializing verification context", STIR_SHAKEN_ERROR_SSL);
		
		// Can use X509_STORE_unlock(g->store); for more fine grained locking later
		pthread_mutex_unlock(&g->mutex);
		return STIR_SHAKEN_STATUS_TERM;
	}

	X509_STORE_CTX_set_ex_data(cert->verify_ctx, 0, file);

	rc = X509_verify_cert(cert->verify_ctx);
	if (rc != 1) {
		// TODO double check if it's a good idea to read verification error from ctx here, outside of verification callback
		verify_error = X509_STORE_CTX_get_error(cert->verify_ctx);
		sprintf(err_buf, "SSL: Bad X509 certificate path: SSL reason: %s\n", X509_verify_cert_error_string(verify_error));
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_CERT_INVALID);
	}
	
	X509_STORE_CTX_cleanup(cert->verify_ctx);
	X509_STORE_CTX_free(cert->verify_ctx);
	cert->verify_ctx = NULL;

	// Can use X509_STORE_unlock(g->store); for more fine grained locking later
	pthread_mutex_unlock(&g->mutex);
	return rc == 1 ? STIR_SHAKEN_STATUS_OK : STIR_SHAKEN_STATUS_FALSE;
}

stir_shaken_status_t stir_shaken_verify_cert_tn_authlist_extension(stir_shaken_context_t *ss, stir_shaken_cert_t *cert)
{
	X509            *x = NULL;

	stir_shaken_clear_error(ss);

	if (!cert || !cert->x) {

		stir_shaken_set_error(ss, "Cert not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}

#if USE_TN_AUTH_LIST_OID
	if (X509_get_ext_by_NID(cert->x, stir_shaken_globals.tn_authlist_nid, -1) == -1) {

		stir_shaken_set_error(ss, "Cert must have ext-tnAuthList extension (OID 1.3.6.1.5.5.7.1.26: http://oid-info.com/get/1.3.6.1.5.5.7.1.26) but it is missing", STIR_SHAKEN_ERROR_TNAUTHLIST);
		return STIR_SHAKEN_STATUS_FALSE;
	}
#endif

	return STIR_SHAKEN_STATUS_OK;
}

stir_shaken_status_t stir_shaken_verify_cert(stir_shaken_context_t *ss, stir_shaken_cert_t *cert)
{
	X509            *x = NULL;

	stir_shaken_clear_error(ss);

	if (!cert) {
		stir_shaken_set_error(ss, "Cert not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_TERM;
	}

	if (STIR_SHAKEN_STATUS_OK != stir_shaken_verify_cert_tn_authlist_extension(ss, cert)) {
		stir_shaken_set_error(ss, "Cert must have ext-tnAuthList extension (OID 1.3.6.1.5.5.7.1.26: http://oid-info.com/get/1.3.6.1.5.5.7.1.26) but it is missing", STIR_SHAKEN_ERROR_TNAUTHLIST);
		return STIR_SHAKEN_STATUS_FALSE;
	}
	
	// TODO pass CAs list and revocation list
	if (!STIR_SHAKEN_MOCK_VERIFY_CERT_CHAIN 
			&& (STIR_SHAKEN_STATUS_OK != stir_shaken_verify_cert_path(ss, cert))) {
		stir_shaken_set_error_if_clear(ss, "Cert did not pass X509 path validation against CA list and CRL", STIR_SHAKEN_ERROR_CERT_INVALID);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	return STIR_SHAKEN_STATUS_OK;
}

char* stir_shaken_cert_get_serialHex(stir_shaken_cert_t *cert)
{
	if (!cert) return NULL;
	return cert->serialHex;
}

char* stir_shaken_cert_get_serialDec(stir_shaken_cert_t *cert)
{
	if (!cert) return NULL;
	return cert->serialDec;
}

char* stir_shaken_cert_get_notBefore(stir_shaken_cert_t *cert)
{
	if (!cert) return NULL;
	return cert->notBefore;
}

char* stir_shaken_cert_get_notAfter(stir_shaken_cert_t *cert)
{
	if (!cert) return NULL;
	return cert->notAfter;
}

char* stir_shaken_cert_get_issuer(stir_shaken_cert_t *cert)
{
	if (!cert) return NULL;
	return cert->issuer;
}

char* stir_shaken_cert_get_subject(stir_shaken_cert_t *cert)
{
	if (!cert) return NULL;
	return cert->subject;
}

int stir_shaken_cert_get_version(stir_shaken_cert_t *cert)
{
	if (!cert) return -1;
	return cert->version;
}

/**
 * @buf - (out) will contain fingerprint, must be of size at least 3*EVP_MAX_MD_SIZE bytes
 * @buflen - (out) will contain string len including '\0'
 */
stir_shaken_status_t stir_shaken_extract_fingerprint(stir_shaken_context_t *ss, X509* x509, const char *digest_name, char *buf, int *buflen)
{
	const EVP_MD *evp = NULL;
	unsigned int i = 0, j = 0;
	uint8_t raw[EVP_MAX_MD_SIZE] = { 0 };

	if (!x509 || !buf || !buflen || !(evp = EVP_get_digestbyname(digest_name))) {
		return STIR_SHAKEN_STATUS_FALSE;
	}

	if (X509_digest(x509, evp, raw, buflen) != 1 ||  buflen <= 0) {
		
		stir_shaken_set_error(ss, "Extract_fingerprint: Error in SSL while extracting digest", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	for (i = 0, j = 0; i < *buflen; ++i, j += 3) {
		sprintf((char*) &buf[j], (i == (*buflen - 1)) ? "%.2X" : "%.2X:", raw[i] & 0xff);
	}
	*(&buf[j - 1]) = '\0';

	*buflen = j;

	return STIR_SHAKEN_STATUS_OK;
}

X509* stir_shaken_make_cert_from_public_key(stir_shaken_context_t *ss, EVP_PKEY *pkey)
{
	X509 *x = X509_new();
	if (!x) {

		stir_shaken_set_error(ss, "Make cert from public key: Failed to  create new X509 certificate", STIR_SHAKEN_ERROR_SSL);
		return NULL;
	}

	X509_set_pubkey(x, pkey);
	return x;
}

stir_shaken_status_t stir_shaken_x509_cert_to_disk(stir_shaken_context_t *ss, X509 *x, const char *cert_full_name, const char *cert_text_full_name)
{
	BIO             *out = NULL, *bio = NULL;
	int				i = 0;
	char			err_buf[STIR_SHAKEN_ERROR_BUF_LEN] = { 0 };
	
	
	stir_shaken_clear_error(ss);

	if (cert_full_name) {
		
		out = BIO_new_fp(stdout, 0);

		i = BIO_write_filename(out, (char*) cert_full_name);
		if (i == 0) {
			
			sprintf(err_buf, "Failed to redirect bio to file %s", cert_full_name);
			stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);
			goto fail;
		}

		i = PEM_write_bio_X509(out, x);
		if (i == 0) {
			
			sprintf(err_buf, "Error writing certificate to file %s", cert_full_name);
			stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);
			goto fail;
		}

		//TODO remove
		printf("STIR-Shaken: Written X509 certificate to file %s\n", cert_full_name);
	}

	BIO_free_all(out);
	out = NULL;

	if (cert_text_full_name) {
		
		bio = BIO_new_file(cert_text_full_name, "w");
		if (!bio) {
		
			stir_shaken_set_error(ss, "error [1]", STIR_SHAKEN_ERROR_SSL);
			goto anyway;
		}

		// TODO This call leaks EVP_PKEY which it creates
		// ==17095==    at 0x4C28C20: malloc (vg_replace_malloc.c:296)
		// ==17095==    by 0x581D377: CRYPTO_malloc (in /usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.0)
		// ==17095==    by 0x58B45D9: EVP_PKEY_new (in /usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.0)
		// ==17095==    by 0x58C3C77: X509_PUBKEY_get (in /usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.0)
		// ==17095==    by 0x58C8069: X509_print_ex (in /usr/lib/x86_64-linux-gnu/libcrypto.so.1.0.0)
		// ==17095==    by 0x4E3BA66: stir_shaken_generate_cert_from_csr (stir_shaken_ssl.c:635)
		X509_print_ex(bio, x, XN_FLAG_COMPAT, X509_FLAG_COMPAT);

		BIO_free_all(bio);
		bio = NULL;

		// Need to decrement ref count of pubkey created by a call to X509_PUBKEY_get in X509_print_ex, so the pubkey is freed
		//k = X509_get0_pubkey(x);
		//EVP_PKEY_free(k);

		// TODO remove
		printf("STIR-Shaken: Written X509 certificate in human readable form to file %s\n", cert_text_full_name);
	}

anyway:
	if (bio) {
		BIO_free_all(bio);
		bio = NULL;
	}
	if (out) {
		BIO_free_all(out);
		out = NULL;
	}
	
	return STIR_SHAKEN_STATUS_OK;

fail:
	if (bio) {
		BIO_free_all(bio);
		bio = NULL;
	}
	if (out) {
		BIO_free_all(out);
		out = NULL;
	}
	if (x) {
		X509_free(x);
		x = NULL;
	}
	
	stir_shaken_set_error_if_clear(ss, "Failed to save X509 cert to disk", STIR_SHAKEN_ERROR_GENERAL);

	return STIR_SHAKEN_STATUS_FALSE;
}

void stir_shaken_destroy_cert(stir_shaken_cert_t *cert)
{
	if (cert) {
		// If X509 gets destroyed then notBefore_ASN1 and notAfter_ASN1 must be NULLED as those are internal pointers to SSL
		if (cert->x) {
			X509_free(cert->x);
			cert->x = NULL;

			// If X509 gets destroyed then notBefore_ASN1 and notAfter_ASN1 must be NULLED as those are internal pointers to SSL
			cert->notBefore_ASN1 = NULL;
			cert->notAfter_ASN1 = NULL;
		}

		if (cert->body) {
			free(cert->body);
			cert->body = NULL;
		}
		if (cert->name) {
			free(cert->name);
			cert->name = NULL;
		}
		if (cert->full_name) {
			free(cert->full_name);
			cert->full_name = NULL;
		}
		if (cert->original_name) {
			free(cert->original_name);
			cert->original_name = NULL;
		}
		if (cert->install_dir) {
			free(cert->install_dir);
			cert->install_dir = NULL;
		}
		if (cert->install_url) {
			free(cert->install_url);
			cert->install_url = NULL;
		}
		if (cert->public_url) {
			free(cert->public_url);
			cert->public_url = NULL;
		}
		stir_shaken_destroy_cert_fields(cert);

		if (cert->verify_ctx) {
			X509_STORE_CTX_cleanup(cert->verify_ctx);
			X509_STORE_CTX_free(cert->verify_ctx);
			cert->verify_ctx = NULL;
		}
	}
}

// TODO
// Robust version should read cert store to allow for cert/key file pack
//
// Something like
// X509_STORE  *cts = SSL_CTX_get_cert_store(ctx.native_handle());
// if(!cts || !cbio)
//		return false;
//		X509_INFO *itmp;
//		int i, count = 0, type = X509_FILETYPE_PEM;
//		STACK_OF(X509_INFO) *inf = PEM_X509_INFO_read_bio(cbio, NULL, NULL, NULL);
//		
//		iterate over all entries from the pem file, add them to the x509_store one by one
//
// see: https://stackoverflow.com/questions/3810058/read-certificate-files-from-memory-instead-of-a-file-using-openssl
//
stir_shaken_status_t stir_shaken_load_cert_from_mem(stir_shaken_context_t *ss, X509 **x, STACK_OF(X509) **xchain, void *mem, size_t n)
{
	stir_shaken_status_t ss_status = STIR_SHAKEN_STATUS_OK;
	BIO	*cbio = NULL;
	STACK_OF(X509)		*stack = NULL;
	STACK_OF(X509_INFO)	*sk = NULL;
	X509_INFO			*xi = NULL;
	
	stir_shaken_clear_error(ss);

	cbio = BIO_new_mem_buf(mem, -1);
	if (!cbio) {
		stir_shaken_set_error(ss, "(SSL) Failed to create BIO", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	// Load end-entity certificate
	*x = PEM_read_bio_X509(cbio, NULL, 0, NULL);

	if (xchain) {

		// Parse untrusted certificate chain

		stack = sk_X509_new_null();
		if (!stack) {
			stir_shaken_set_error(ss, "Failed to allocate cert stack", STIR_SHAKEN_ERROR_SSL);
			X509_free(*x);
			ss_status = STIR_SHAKEN_STATUS_TERM;
			goto exit;
		}

		sk = PEM_X509_INFO_read_bio(cbio, NULL, NULL, NULL);
		if (!sk) {
			stir_shaken_set_error(ss, "Error reading certificate stack", STIR_SHAKEN_ERROR_SSL);
			X509_free(*x);
			sk_X509_free(stack);
			ss_status = STIR_SHAKEN_STATUS_FALSE;
			goto exit;
		}

		while (sk_X509_INFO_num(sk)) {
			
			xi = sk_X509_INFO_shift(sk);
			
			if (xi->x509 != NULL) {
				
				sk_X509_push(stack, xi->x509);
				xi->x509 = NULL;
			}

			X509_INFO_free(xi);
		}

		if (!sk_X509_num(stack)) {
			
			sk_X509_free(stack);

		} else {

			*xchain = stack;
		}

		sk_X509_INFO_free(sk);
	}

exit:
	if (cbio) {
		BIO_free(cbio);
		cbio = NULL;
	}
	return ss_status;
}

stir_shaken_status_t stir_shaken_load_cert_from_file(stir_shaken_context_t *ss, X509 **x, const char *cert_tmp_name)
{
	BIO				*in = NULL;
	char			err_buf[STIR_SHAKEN_ERROR_BUF_LEN] = { 0 };
	
	
	stir_shaken_clear_error(ss);

	in = BIO_new(BIO_s_file());
	if (!in) {
		stir_shaken_set_error(ss, "(SSL) Failed to create BIO", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	if (BIO_read_filename(in, cert_tmp_name) <= 0) {
		sprintf(err_buf, "Error reading file: %s", cert_tmp_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	*x = PEM_read_bio_X509(in, NULL, NULL, NULL);
	if (*x == NULL) {
		sprintf(err_buf, "(SSL) Failed to read X509 from file: %s", cert_tmp_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);
		BIO_free(in);
		in = NULL;
		return STIR_SHAKEN_STATUS_FALSE;
	}

	BIO_free(in); in = NULL;
	return STIR_SHAKEN_STATUS_OK;
}

stir_shaken_status_t stir_shaken_load_cert_and_key(stir_shaken_context_t *ss, const char *cert_name, stir_shaken_cert_t *cert, const char *private_key_name, EVP_PKEY **pkey, unsigned char *priv_raw, uint32_t *priv_raw_len)
{
	X509            *x = NULL;
	BIO             *in = NULL;
	char			*b = NULL;
	char			err_buf[STIR_SHAKEN_ERROR_BUF_LEN] = { 0 };
	
	
	stir_shaken_clear_error(ss);

	if (!cert_name) {
		
		stir_shaken_set_error(ss, "Load cert and key: Cert name not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	if (!cert) {
		stir_shaken_set_error(ss, "Load cert and key: Cert not set", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_FALSE;
	}
	
	b = basename((char*) cert_name);

	memset(cert, 0, sizeof(stir_shaken_cert_t));
	cert->full_name = malloc(strlen(cert_name) + 1);
	cert->name = malloc(strlen(b) + 1);

	if (!cert->name || !cert->full_name) {
		
		stir_shaken_set_error(ss, "Load cert and key: Out of memory [2]", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_FALSE;
	}
	
	memcpy(cert->full_name, cert_name, strlen(cert_name));
	cert->full_name[strlen(cert_name)] = '\0';

	memcpy(cert->name, b, strlen(b));
	cert->name[strlen(b)] = '\0';

	if (stir_shaken_file_exists(cert_name) != STIR_SHAKEN_STATUS_OK) {
		
		sprintf(err_buf, "Load cert and key: File doesn't exist: %s", cert_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_GENERAL);

		goto err;
	}

	if (stir_shaken_file_exists(private_key_name) != STIR_SHAKEN_STATUS_OK) {
		
		sprintf(err_buf, "Load cert and key: File doesn't exist: %s", private_key_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_GENERAL);

		goto err;
	}

	in = BIO_new(BIO_s_file());
	if (BIO_read_filename(in, private_key_name) <= 0) {
		
		sprintf(err_buf, "Load cert and key: Error reading file: %s", private_key_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);

		goto err;
	}

	*pkey = PEM_read_bio_PrivateKey(in, NULL, NULL, NULL);
	if (*pkey == NULL) {
		sprintf(err_buf, "Load cert and key: Error geting SSL key from file: %s", private_key_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);
		goto err;
	}
	BIO_free(in); in = NULL;

	if (priv_raw) {

		FILE *fp = NULL;
		uint32_t raw_key_len = 0, sz = 0;

		if (!priv_raw_len || *priv_raw_len == 0) {
			sprintf(err_buf, "Generate keys: Buffer for %s invalid", private_key_name);
			stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_GENERAL);
			goto err;
		}

		// Read raw private key into buffer
		fp = fopen(private_key_name, "r");
		if (!fp) {
			sprintf(err_buf, "Generate keys: Cannot open private key %s for reading", private_key_name);
			stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_GENERAL);
			goto err;
		}

		fseek(fp, 0, SEEK_END);
		sz = ftell(fp);
		rewind(fp);
		
		if (*priv_raw_len <= sz) {
			sprintf(err_buf, "Generate keys: Buffer for %s too short", private_key_name);
			stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_GENERAL);
			goto err;
		}

		// TODO remove
		printf("Reading raw key from: %s, which is %zu bytes\n", private_key_name, sz);

		raw_key_len = fread(priv_raw, 1, *priv_raw_len, fp);
		if (raw_key_len != sz || ferror(fp)) {
			sprintf(err_buf, "Generate keys: Error reading private key %s, which is %zu bytes", private_key_name, sz);
			stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_GENERAL);
			goto err;
		}

		fclose(fp);
		fp = NULL;

		priv_raw[raw_key_len] = '\0';
		*priv_raw_len = raw_key_len;
	}


	in = BIO_new(BIO_s_file());
	if (!in) {
		stir_shaken_set_error(ss, "SSL: Failed to create BIO", STIR_SHAKEN_ERROR_SSL);
		goto err;
	}

	if (BIO_read_filename(in, cert_name) <= 0) {
		sprintf(err_buf, "Failed to read file: %s. Does it exist?", cert_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);
		goto err;
	}

	x = PEM_read_bio_X509(in, NULL, 0, NULL);
	if (!x) {
		sprintf(err_buf, "(SSL) Failed to read X509 from file: %s", cert_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);
		goto err;
	}

	cert->x = x;
	cert->private_key = *pkey;

	BIO_free(in);
	in = NULL;

	return STIR_SHAKEN_STATUS_OK;

err:
	
	if (cert->full_name) free(cert->full_name);
	cert->full_name = NULL;
	if (cert->name) free(cert->name);
	cert->name = NULL;
	stir_shaken_set_error_if_clear(ss, "Load cert and key: Error", STIR_SHAKEN_ERROR_GENERAL);

	return STIR_SHAKEN_STATUS_FALSE;
}

stir_shaken_status_t stir_shaken_generate_keys(stir_shaken_context_t *ss, EC_KEY **eck, EVP_PKEY **priv, EVP_PKEY **pub, const char *private_key_full_name, const char *public_key_full_name, unsigned char *priv_raw, uint32_t *priv_raw_len)
{
	EC_KEY                  *ec_key = NULL;
	EVP_PKEY                *pk = NULL;
	BIO                     *out = NULL, *bio = NULL, *key = NULL;
	char					err_buf[STIR_SHAKEN_ERROR_BUF_LEN] = { 0 };
	int						pkey_type = EVP_PKEY_EC;
	FILE					*fp = NULL;


	stir_shaken_clear_error(ss);
	memset(err_buf, 0, sizeof(err_buf));

	if (!stir_shaken_globals.initialised) {
		stir_shaken_set_error(ss, "Generate keys: STIR-Shaken library not initialised", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_RESTART;
	}

	if (eck == NULL || priv == NULL || pub == NULL || private_key_full_name == NULL || public_key_full_name == NULL) {
		stir_shaken_set_error(ss, "Generate keys: Bad params", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	// Keys should be NULL, otherwise we could overwrite them, but probably better to require user to know that it is not going to happen
	if (*eck) {
		stir_shaken_set_error(ss, "Generate keys: Bad params: EC KEY is set but should be NULL", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_FALSE;
	}
	if (*pub) {
		stir_shaken_set_error(ss, "Generate keys: Bad params: EVP KEY (public) is set but should be NULL", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_FALSE;
	}
	if (*priv) {
		stir_shaken_set_error(ss, "Generate keys: Bad params: EVP KEY (private) is set but should be NULL", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	// file_remove(private_key_full_name, NULL);
	// file_remove(public_key_full_name, NULL);

	/* Generate EC key associated with our chosen curve. */
	ec_key = EC_KEY_new_by_curve_name(stir_shaken_globals.curve_nid);
	if (!ec_key) {
		stir_shaken_set_error(ss, "Generate keys: SSL ERR: Cannot construct new EC key", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}

	// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Got new EC\n");
	*eck = ec_key;

	if (!EC_KEY_generate_key(ec_key)) {
		stir_shaken_set_error(ss, "Generate keys: SSL ERR: Cannot generate new private/public keys from EC key", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}
	
	// TODO remove
	printf("STIR-Shaken: SSL: Got new private/public EC key pair\n");

	if (!EC_KEY_check_key(ec_key)) {
		stir_shaken_set_error(ss, "Generate keys: SSL ERR: EC key pair is invalid", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}
	
	// TODO remove
	printf("STIR-Shaken: SSL: Private/public EC key pair is OK\n");

	// Print them out
	out = BIO_new_fp(stdout, BIO_NOCLOSE);
	PEM_write_bio_ECPrivateKey(out, ec_key, NULL, NULL, 0, NULL, NULL);
	PEM_write_bio_EC_PUBKEY(out, ec_key);
	BIO_free_all(out);
	out = NULL;

	// Save
	bio = BIO_new_file(private_key_full_name, "w");
	if (!bio) {
		stir_shaken_set_error(ss, "Generate keys: SSL ERR: Cannot open private key into bio", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}
	PEM_write_bio_ECPrivateKey(bio, ec_key, NULL, NULL, 0, NULL, NULL);
	BIO_free_all(bio);
	bio = NULL;

	bio = BIO_new_file(public_key_full_name, "w");
	if (!bio) {
		stir_shaken_set_error(ss, "Generate keys: SSL ERR: Cannot open public key into bio", STIR_SHAKEN_ERROR_GENERAL);
		goto fail;
	}
	PEM_write_bio_EC_PUBKEY(bio, ec_key);
	BIO_free_all(bio);
	bio = NULL;

	// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Saved private key: %s\n", private_key_full_name); printf("STIR-Shaken: SSL: Saved public key: %s\n", public_key_full_name);
	
	key = BIO_new(BIO_s_file());
	if (BIO_read_filename(key, private_key_full_name) <= 0) {
		stir_shaken_set_error(ss, "Generate keys: SSL ERR: Err, bio read priv key", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}
	pk = PEM_read_bio_PrivateKey(key, NULL, NULL, NULL);
	if (pk == NULL) {
		
		sprintf(err_buf, "Generate keys: Error geting SSL key from file: %s", private_key_full_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);

		goto fail;
	}
	*priv = pk;
	pkey_type = EVP_PKEY_id(pk);
	if (pkey_type != EVP_PKEY_EC) {

		sprintf(err_buf, "Generate keys: Private key is not EVP_PKEY_EC type");
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}
	
	// TODO remove
	printf("Generate keys: Private key is EVP_PKEY_EC type\n");

	// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Loaded pkey from: %s\n", private_key_full_name);
	BIO_free_all(key);
	key = NULL;

	if (priv_raw) {

		FILE *fp = NULL;
		uint32_t raw_key_len = 0, sz = 0;

		if (!priv_raw_len || *priv_raw_len == 0) {
			
			sprintf(err_buf, "Generate keys: Buffer for %s invalid", private_key_full_name);
			stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_GENERAL);
			goto fail;
		}

		// Read raw private key into buffer
		fp = fopen(private_key_full_name, "r");
		if (!fp) {

			sprintf(err_buf, "Generate keys: Cannot open private key %s for reading", private_key_full_name);
			stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_GENERAL);
			goto fail;
		}

		fseek(fp, 0, SEEK_END);
		sz = ftell(fp);
		rewind(fp);
		
		if (*priv_raw_len <= sz) {
			
			sprintf(err_buf, "Generate keys: Buffer for %s too short", private_key_full_name);
			stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_GENERAL);
			goto fail;
		}

		// TODO remove
		printf("Reading raw key from: %s, which is %zu bytes\n", private_key_full_name, sz);

		raw_key_len = fread(priv_raw, 1, *priv_raw_len, fp);
		if (raw_key_len != sz || ferror(fp)) {

			sprintf(err_buf, "Generate keys: Error reading private key %s, which is %zu bytes", private_key_full_name, sz);
			stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_GENERAL);
			goto fail;
		}

		fclose(fp);
		fp = NULL;

		priv_raw[raw_key_len] = '\0';
		*priv_raw_len = raw_key_len;
	}

	key = BIO_new(BIO_s_file());
	if (BIO_read_filename(key, public_key_full_name) <= 0) {
		stir_shaken_set_error(ss, "Generate keys: SSL ERR: Err, bio read public key", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}
	pk = PEM_read_bio_PUBKEY(key, NULL, NULL, NULL);
	if (pk == NULL) {
		
		sprintf(err_buf, "Generate keys: Error geting SSL key from file: %s", public_key_full_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);

		goto fail;
	}
	*pub = pk;
	pkey_type = EVP_PKEY_id(pk);
	if (pkey_type != EVP_PKEY_EC) {

		sprintf(err_buf, "Generate keys: Public key is not EVP_PKEY_EC type");
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);

		goto fail;
	}
	
	// TODO remove
	printf("Generate keys: Public key is EVP_PKEY_EC type\n");

	// TODO set error string, allow for retrieval printf("STIR-Shaken: SSL: Loaded pkey from: %s\n", public_key_full_name);

	if (key) BIO_free_all(key); key = NULL;
	if (out) BIO_free_all(out); out = NULL;
	if (bio) BIO_free_all(bio); bio = NULL;

	return STIR_SHAKEN_STATUS_OK;

fail:

	if (key) BIO_free_all(key); key = NULL;
	if (out) BIO_free_all(out); out = NULL;
	if (bio) BIO_free_all(bio); bio = NULL;
	stir_shaken_set_error_if_clear(ss, "Generate keys: Error", STIR_SHAKEN_ERROR_GENERAL);

	return STIR_SHAKEN_STATUS_FALSE;
}

void stir_shaken_destroy_keys(EC_KEY **eck, EVP_PKEY **priv, EVP_PKEY **pub)
{
	if (eck && *eck) {
		EC_KEY_free(*eck);
		*eck = NULL;
	}
	if (priv && *priv) {
		EVP_PKEY_free(*priv);
		*priv = NULL;
	}
	if (pub && *pub) {
		EVP_PKEY_free(*pub);
		*pub = NULL;
	}
	ERR_free_strings();
    EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();
	ENGINE_cleanup();
}

/**
 * Using @digest_name and @pkey create a signature for @data and save it in @out.
 * Return @out and length of it in @outlen.
 */ 
stir_shaken_status_t stir_shaken_do_sign_data_with_digest(stir_shaken_context_t *ss, const char *digest_name, EVP_PKEY *pkey, const char *data, size_t datalen, unsigned char *out, size_t *outlen)
{
	// TODO: JWS signature
	// JWS Signature = ES256(ASCII(BASE64URL(UTF8(JWS Protected Header)) || "." || BASE64URL(JWS Payload)))
	// JWS Signature = ES256(Main Signature)
	//
	//    +--------------+-------------------------------+--------------------+
	//    | "alg" Param  | Digital Signature or MAC      | Implementation     |
	//    | Value        | Algorithm                     | Requirements       |
	//    +--------------+-------------------------------+--------------------+
	//    | HS256        | HMAC using SHA-256            | Required           |
	//    | HS384        | HMAC using SHA-384            | Optional           |
	//    | HS512        | HMAC using SHA-512            | Optional           |
	//    | RS256        | RSASSA-PKCS1-v1_5 using       | Recommended        |
	//    |              | SHA-256                       |                    |
	//    | RS384        | RSASSA-PKCS1-v1_5 using       | Optional           |
	//    |              | SHA-384                       |                    |
	//    | RS512        | RSASSA-PKCS1-v1_5 using       | Optional           |
	//    |              | SHA-512                       |                    |
	//    | ES256        | ECDSA using P-256 and SHA-256 | Recommended+ 

	const EVP_MD    *md = NULL;
	EVP_MD_CTX      *mdctx = NULL;
	int             i = 0;
	char			err_buf[STIR_SHAKEN_ERROR_BUF_LEN] = { 0 };
	unsigned char	*tmpsig = NULL;
	size_t			tmpsig_len = 0;
	ECDSA_SIG		*ec_sig = NULL;


	stir_shaken_clear_error(ss);

	if (!pkey || !data || !out || !outlen) {
		stir_shaken_set_error(ss, "Do sign data with digest: Bad params", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_FALSE;
	}

	md = EVP_get_digestbyname(digest_name);
	if (!md) {
		sprintf(err_buf, "Do sign data with digest: Cannot get %s digest", digest_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL);
		goto err;
	}

	mdctx = EVP_MD_CTX_create();
	if (!mdctx) {
		stir_shaken_set_error(ss, "Do sign data with digest: Cannot get md context", STIR_SHAKEN_ERROR_SSL);
		goto err;
	}
	EVP_MD_CTX_init(mdctx);
	i = EVP_DigestSignInit(mdctx, NULL, md, NULL, pkey);
	if (i == 0) {
		stir_shaken_set_error(ss, "Do sign data with digest: Error in EVP_DigestSignInit", STIR_SHAKEN_ERROR_SSL);
		goto err;
	}
	i = EVP_DigestSignUpdate(mdctx, data, datalen);
	if (i == 0) {
		stir_shaken_set_error(ss, "Do sign data with digest: Error in EVP_DigestSignUpdate", STIR_SHAKEN_ERROR_SSL);
		goto err;
	}
	
	/* First, call EVP_DigestSignFinal with a NULL sig parameter to get length
	 * of sig. Length is returned in slen */
	if (EVP_DigestSignFinal(mdctx, NULL, &tmpsig_len) != 1) {
		stir_shaken_set_error(ss, "Do sign data with digest: Error in EVP_DigestSignFinal while getting sig len", STIR_SHAKEN_ERROR_SSL);
		goto err;
	}

	/* Allocate memory for signature based on returned size */
	tmpsig = alloca(tmpsig_len);
	if (!tmpsig) {
		stir_shaken_set_error(ss, "Do sign data with digest: Cannot allocate memory for signature", STIR_SHAKEN_ERROR_SSL);
		goto err;
	}

	i = EVP_DigestSignFinal(mdctx, tmpsig, &tmpsig_len);
	if (i == 0 || (tmpsig_len >= PBUF_LEN - 1)) {
		stir_shaken_set_error(ss, "Do sign data with digest: Error in EVP_DigestSignFinal", STIR_SHAKEN_ERROR_SSL);
		goto err;
	}
	tmpsig[tmpsig_len] = '\0';
	EVP_MD_CTX_destroy(mdctx);
	mdctx = NULL;

	if (tmpsig_len > 0) {
		
		unsigned int	degree = 0, bn_len = 0, r_len = 0, s_len = 0, buf_len = 0;
		unsigned char	*raw_buf = NULL, *sig = NULL;
		size_t			slen = 0;
		EC_KEY			*ec_key = NULL;
		const BIGNUM	*ec_sig_r = NULL;
		const BIGNUM	*ec_sig_s = NULL;

		/* For EC we need to convert to a raw format of R/S. */

		/* Get the actual ec_key */
		ec_key = EVP_PKEY_get1_EC_KEY(pkey);
		if (ec_key == NULL) {
			stir_shaken_set_error(ss, "Do sign data with digest: Cannot get EC key from EVP key", STIR_SHAKEN_ERROR_SSL);
			goto err;
		}

		degree = EC_GROUP_get_degree(EC_KEY_get0_group(ec_key));

		EC_KEY_free(ec_key);

		/* Get the sig from the DER encoded version. */
		ec_sig = d2i_ECDSA_SIG(NULL, (const unsigned char **) &tmpsig, tmpsig_len);
		if (ec_sig == NULL) {
			stir_shaken_set_error(ss, "Do sign data with digest: Cannot get signature from DER", STIR_SHAKEN_ERROR_SSL);
			goto err;
		}

		ECDSA_SIG_get0(ec_sig, &ec_sig_r, &ec_sig_s);
		r_len = BN_num_bytes(ec_sig_r);
		s_len = BN_num_bytes(ec_sig_s);
		bn_len = (degree + 7) / 8;
		if ((r_len > bn_len) || (s_len > bn_len)) {
			stir_shaken_set_error(ss, "Do sign data with digest: Algorithm/key/method  misconfiguration", STIR_SHAKEN_ERROR_SSL);
			goto err;
		}

		buf_len = 2 * bn_len;
		raw_buf = alloca(buf_len);
		if (raw_buf == NULL) {
			stir_shaken_set_error(ss, "Do sign data with digest: Out of mem", STIR_SHAKEN_ERROR_GENERAL);
			goto err;
		}

		/* Pad the bignums with leading zeroes. */
		memset(raw_buf, 0, buf_len);
		BN_bn2bin(ec_sig_r, raw_buf + bn_len - r_len);
		BN_bn2bin(ec_sig_s, raw_buf + buf_len - s_len);

		if (buf_len > *outlen) {
			stir_shaken_set_error(ss, "Do sign data with digest: Output buffer too short", STIR_SHAKEN_ERROR_GENERAL);
			goto err;
		}

		if (ec_sig) {
			ECDSA_SIG_free(ec_sig);
			ec_sig = NULL;
		}

		memcpy(out, raw_buf, buf_len);
		*outlen = buf_len;
	}

	if (mdctx) {
		EVP_MD_CTX_destroy(mdctx);
		mdctx = NULL;
	}
	if (ec_sig) {
		ECDSA_SIG_free(ec_sig);
		ec_sig = NULL;
	}
	ERR_free_strings();
    EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();
	ENGINE_cleanup();

	return STIR_SHAKEN_STATUS_OK;

err:
	stir_shaken_set_error_if_clear(ss, "Do sign data with digest: Error", STIR_SHAKEN_ERROR_SSL);
	if (ec_sig) {
		ECDSA_SIG_free(ec_sig);
		ec_sig = NULL;
	}
	if (mdctx) {
		EVP_MD_CTX_destroy(mdctx);
		mdctx = NULL;
	}
	ERR_free_strings();
    EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();
	ENGINE_cleanup();
	return STIR_SHAKEN_STATUS_FALSE;
}

int stir_shaken_do_verify_data(stir_shaken_context_t *ss, const void *data, size_t datalen, const unsigned char *sig, size_t siglen, EVP_PKEY *public_key)
{
	BIO *bio_err = NULL;
	const EVP_MD    *md = NULL;
	EVP_MD_CTX *mctx = NULL;
	EVP_PKEY_CTX *pctx = NULL;
	int r = -1;
	int res = -1;
	char			err_buf[STIR_SHAKEN_ERROR_BUF_LEN] = { 0 };
	const char      *digest_name = "sha256";
	unsigned char	*tmpsig = NULL;
	size_t			tmpsig_len = 0;
	ECDSA_SIG		*ec_sig = NULL;

	stir_shaken_clear_error(ss);

	if (!data || !sig || siglen == 0 || !public_key) {
		stir_shaken_set_error(ss, "Do verify data: Bad params", STIR_SHAKEN_ERROR_GENERAL);
		goto err;
	}

	bio_err = BIO_new(BIO_s_file());
	BIO_set_fp(bio_err, stdout, BIO_NOCLOSE | BIO_FP_TEXT);
	
	/* Convert EC sigs back to ASN1. */
	if (sig) {

		BIGNUM *ec_sig_r = NULL;
		BIGNUM *ec_sig_s = NULL;
		unsigned int degree = 0, bn_len = 0;
		unsigned char *p = NULL;
		EC_KEY *ec_key = NULL;

		ec_sig = ECDSA_SIG_new();
		if (ec_sig == NULL) {
			stir_shaken_set_error(ss, "Do verify data: Cannot create EC signature", STIR_SHAKEN_ERROR_SSL);
			goto err;
		}

		/* Get the actual ec_key */
		ec_key = EVP_PKEY_get1_EC_KEY(public_key);
		if (ec_key == NULL) {
			stir_shaken_set_error(ss, "Do verify data: Cannot create EC key", STIR_SHAKEN_ERROR_SSL);
			goto err;
		}

		degree = EC_GROUP_get_degree(EC_KEY_get0_group(ec_key));

		EC_KEY_free(ec_key);

		bn_len = (degree + 7) / 8;
		if ((bn_len * 2) != siglen) {
			stir_shaken_set_error(ss, "Do verify data: Bad EC key", STIR_SHAKEN_ERROR_SSL);
			goto err;
		}

		ec_sig_r = BN_bin2bn(sig, bn_len, NULL);
		ec_sig_s = BN_bin2bn(sig + bn_len, bn_len, NULL);
		if (ec_sig_r  == NULL || ec_sig_s == NULL) {
			stir_shaken_set_error(ss, "Do sign data with digest: Algorithm/key/method  misconfiguration", STIR_SHAKEN_ERROR_SSL);
			goto err;
		}

		ECDSA_SIG_set0(ec_sig, ec_sig_r, ec_sig_s);

		tmpsig_len = i2d_ECDSA_SIG(ec_sig, NULL);
		tmpsig = alloca(tmpsig_len);
		if (tmpsig == NULL) {
			stir_shaken_set_error(ss, "Do sign data with digest: Out of mem", STIR_SHAKEN_ERROR_GENERAL);
			goto err;
		}

		p = tmpsig;
		tmpsig_len = i2d_ECDSA_SIG(ec_sig, &p);

		if (tmpsig_len == 0) {
			stir_shaken_set_error(ss, "Do sign data with digest: Algorithm/key/method  misconfiguration", STIR_SHAKEN_ERROR_SSL);
			goto err;
		}
	}

	md = EVP_get_digestbyname(digest_name);
	if (!md) {
		sprintf(err_buf, "STIR-Shaken: Cannot get %s digest", digest_name);
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL); 
		goto err;
	}

	mctx = EVP_MD_CTX_create();
	//EVP_DigestInit_ex(mdctx, md, NULL);
	EVP_MD_CTX_init(mctx);

	r = EVP_DigestVerifyInit(mctx, &pctx, md, NULL, public_key);
	if (r <= 0) {
		sprintf(err_buf, "STIR-Shaken: Error setting context");
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL); 
		goto err;
	}

	r = EVP_DigestVerifyUpdate(mctx, (const void*)data, datalen);
	if (r <= 0) {
		sprintf(err_buf, "STIR-Shaken: Error updating context");
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL); 
		goto err;
	}

	r = EVP_DigestVerifyFinal(mctx, (unsigned char*)tmpsig, (unsigned int)tmpsig_len);
	if (r > 0) {
		// OK
		res = 0;
	} else if (r == 0) {
		sprintf(err_buf, "Signature/data-key failed verification (signature doesn't match the data-key pair)");
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SIP_438_INVALID_IDENTITY_HEADER); 
		res = 1;
	} else {
		sprintf(err_buf, "Unknown error while verifying data");
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL); 
		res = 2;
		ERR_print_errors(bio_err);
	}

	if (mctx) {
		EVP_MD_CTX_destroy(mctx);
		mctx = NULL;
	}
	if (bio_err) {
		BIO_free(bio_err);
	}
	if (ec_sig) {
		ECDSA_SIG_free(ec_sig);
		ec_sig = NULL;
	}
	ERR_free_strings();
    EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();
	ENGINE_cleanup();
	return res;

err:
	if (mctx) {
		EVP_MD_CTX_destroy(mctx);
		mctx = NULL;
	}
	if (bio_err) {
		BIO_free(bio_err);
		bio_err = NULL;
	}
	if (ec_sig) {
		ECDSA_SIG_free(ec_sig);
		ec_sig = NULL;
	}
	ERR_free_strings();
    EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();
	ENGINE_cleanup();
	stir_shaken_set_error_if_clear(ss, "Do verify data: Error", STIR_SHAKEN_ERROR_SSL);
	return -1;
}

/*
 * @body - (out) buffer for raw csr
 * @body_len - (in/out) on entry buffer length, on return written csr length
 */
stir_shaken_status_t stir_shaken_get_csr_raw(stir_shaken_context_t *ss, X509_REQ *req, unsigned char *body, int *body_len)
{
	BIO *bio = NULL;

	if (!req || !body || !body_len) return STIR_SHAKEN_STATUS_TERM;

	bio = BIO_new(BIO_s_mem());

	if (!bio || (PEM_write_bio_X509_REQ(bio, req) <= 0)) {

		stir_shaken_set_error(ss, "Get csr raw: Failed to write from X509_REQ into memory BIO", STIR_SHAKEN_ERROR_SSL);
		BIO_free_all(bio); bio = NULL;
		return STIR_SHAKEN_STATUS_RESTART;
	}

	*body_len = BIO_read(bio, body, *body_len);
	if (*body_len <= 0) {

		stir_shaken_set_error(ss, "Get csr raw: Failed to read from output memory BIO", STIR_SHAKEN_ERROR_SSL);
		BIO_free_all(bio); bio = NULL;
		return STIR_SHAKEN_STATUS_RESTART;
	}

	BIO_free_all(bio); bio = NULL;
	return STIR_SHAKEN_STATUS_OK;
}

stir_shaken_status_t stir_shaken_get_cert_raw(stir_shaken_context_t *ss, X509 *x, unsigned char *raw, int *raw_len)
{
	BIO *bio = NULL;

	if (!x || !raw || !raw_len) return STIR_SHAKEN_STATUS_TERM;

	bio = BIO_new(BIO_s_mem());

	if (!bio || (PEM_write_bio_X509(bio, x) <= 0)) {

		stir_shaken_set_error(ss, "Get cert raw: Failed to write from X509 into memory BIO", STIR_SHAKEN_ERROR_SSL);
		BIO_free_all(bio); bio = NULL;
		return STIR_SHAKEN_STATUS_RESTART;
	}

	*raw_len = BIO_read(bio, raw, *raw_len);
	if (*raw_len <= 0) {

		stir_shaken_set_error(ss, "Get cert raw: Failed to read from output memory BIO", STIR_SHAKEN_ERROR_SSL);
		BIO_free_all(bio); bio = NULL;
		return STIR_SHAKEN_STATUS_RESTART;
	}

	BIO_free_all(bio); bio = NULL;
	return STIR_SHAKEN_STATUS_OK;
}

/*
 * @key - (out) buffer for raw public key
 * @key_len - (in/out) on entry buffer length, on return read key length
 */
stir_shaken_status_t stir_shaken_evp_key_to_raw(stir_shaken_context_t *ss, EVP_PKEY *evp_key, unsigned char *key, int *key_len)
{
	BIO *bio = NULL;

	if (!evp_key || !key || !key_len) return STIR_SHAKEN_STATUS_TERM;

	bio = BIO_new(BIO_s_mem());

	if (!bio || (PEM_write_bio_PUBKEY(bio, evp_key) <= 0)) {

		stir_shaken_set_error(ss, "Get pubkey raw: Failed to write from EVP_PKEY into memory BIO", STIR_SHAKEN_ERROR_SSL);
		BIO_free_all(bio); bio = NULL;
		return STIR_SHAKEN_STATUS_RESTART;
	}

	*key_len = BIO_read(bio, key, *key_len);
	if (*key_len <= 0) {

		stir_shaken_set_error(ss, "Get pubkey raw: Failed to read from output memory BIO", STIR_SHAKEN_ERROR_SSL);
		BIO_free_all(bio); bio = NULL;
		return STIR_SHAKEN_STATUS_RESTART;
	}

	BIO_free_all(bio); bio = NULL;
	return STIR_SHAKEN_STATUS_OK;
}

/*
 * @key - (out) buffer for raw public key from cert
 * @key_len - (in/out) on entry buffer length, on return read key length
 */
stir_shaken_status_t stir_shaken_get_pubkey_raw_from_cert(stir_shaken_context_t *ss, stir_shaken_cert_t *cert, unsigned char *key, int *key_len)
{
	BIO *bio = NULL;
	EVP_PKEY *pk = NULL;
	stir_shaken_status_t ret = STIR_SHAKEN_STATUS_FALSE;

	if (!cert || !key || !key_len) return STIR_SHAKEN_STATUS_TERM;

	if (!(pk = X509_get_pubkey(cert->x))) {

		stir_shaken_set_error(ss, "Get pubkey raw: Failed to read EVP_PKEY from cert", STIR_SHAKEN_ERROR_SSL);
		BIO_free_all(bio); bio = NULL;
		return STIR_SHAKEN_STATUS_RESTART;
	}
	
	ret = stir_shaken_evp_key_to_raw(ss, pk, key, key_len);
	BIO_free_all(bio); bio = NULL;
	EVP_PKEY_free(pk); pk = NULL;
	return ret;
}

stir_shaken_status_t stir_shaken_create_jwk(stir_shaken_context_t *ss, EC_KEY *ec_key, const char *kid, cJSON **jwk)
{
	cJSON *j = NULL;
	BIGNUM *x = NULL, *y = NULL;
	const EC_GROUP *group = NULL;
	const EC_POINT *point = NULL;
	char *x_b64 = "";
	char *y_b64 = "";

	if (!ec_key || !jwk) return STIR_SHAKEN_STATUS_TERM;

	point = EC_KEY_get0_public_key(ec_key);
	if (!point) {
		stir_shaken_set_error(ss, "Cannot get EC point from EC key", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_ERR;
	}

	group = EC_KEY_get0_group(ec_key);
	if (!group) {
		stir_shaken_set_error(ss, "Cannot get EC group from EC key", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_ERR;
	}

	if (EC_POINT_get_affine_coordinates_GFp(group, point, x, y, NULL) != 1) {
		stir_shaken_set_error(ss, "Cannot get affine coordinates from EC key", STIR_SHAKEN_ERROR_SSL);
		return STIR_SHAKEN_STATUS_ERR;
	}

	// TODO need to get x and y coordinates in base 64

	j = cJSON_CreateObject();
	if (!j) {
		stir_shaken_set_error(ss, "Error in cjson, cannot create object", STIR_SHAKEN_ERROR_CJSON);
		return STIR_SHAKEN_STATUS_ERR;
	}

	cJSON_AddStringToObject(j, "kty", "EC");
	cJSON_AddStringToObject(j, "crv", "P-256");
	cJSON_AddStringToObject(j, "x", x_b64);
	cJSON_AddStringToObject(j, "y", y_b64);
	if (kid) {
		cJSON_AddStringToObject(j, "kid", kid); // kid should be something like "sp.com Reg Public key 123XYZ"
	}

	*jwk = j;

	return STIR_SHAKEN_STATUS_OK;
}

void stir_shaken_print_cert_fields(FILE *file, stir_shaken_cert_t *cert)
{
	if (!cert) return;

	fprintf(file, "STIR-Shaken: STI Cert: Serial number: %s %s\n", stir_shaken_cert_get_serialHex(cert), stir_shaken_cert_get_serialDec(cert));
	fprintf(file, "STIR-Shaken: STI Cert: Issuer: %s\n", stir_shaken_cert_get_issuer(cert));
	fprintf(file, "STIR-Shaken: STI Cert: Subject: %s\n", stir_shaken_cert_get_subject(cert));
	fprintf(file, "STIR-Shaken: STI Cert: Valid from: %s\n", stir_shaken_cert_get_notBefore(cert));
	fprintf(file, "STIR-Shaken: STI Cert: Valid to: %s\n", stir_shaken_cert_get_notAfter(cert));
	fprintf(file, "STIR-Shaken: STI Cert: Version: %d\n", stir_shaken_cert_get_version(cert));
}

/**
 * Setup OpenSSL lib.
 * Must be called locked.
 */
stir_shaken_status_t stir_shaken_init_ssl(stir_shaken_context_t *ss, const char *ca_dir, const char *crl_dir)
{
	const SSL_METHOD        **ssl_method = &stir_shaken_globals.ssl_method;
	SSL_CTX                 **ssl_ctx = &stir_shaken_globals.ssl_ctx;
	SSL                     **ssl = &stir_shaken_globals.ssl;
	EC_builtin_curve        *curves = NULL, *c = NULL, *curve = NULL;
	size_t                  i = 0, n = 0;
	int                     curve_nid = -1;                 // id of the curve in OpenSSL
	char					err_buf[STIR_SHAKEN_ERROR_BUF_LEN] = { 0 };


	stir_shaken_clear_error(ss);

	if (stir_shaken_globals.initialised) {
		stir_shaken_set_error(ss, "Already initialised", STIR_SHAKEN_ERROR_GENERAL);
		return STIR_SHAKEN_STATUS_NOOP;
	}

	// Init libssl
	SSL_library_init();
	//SSL_load_errors();																																// TODO doesn't compile anymore ?

	// Init libcrypto
	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();

	*ssl_method = SSLv23_server_method();                   /* create server instance */

	ERR_clear_error();	
	*ssl_ctx = SSL_CTX_new(*ssl_method);                    /* create context */
	if (!*ssl_ctx) {
		//sprintf(err_buf, "SSL ERR: Failed to init SSL context, SSL error: %s", ERR_error(ERR_get_error(), NULL)); 									// TODO doesn't compile anymore ?
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL); 
		return STIR_SHAKEN_STATUS_FALSE;
	}

	*ssl = SSL_new(*ssl_ctx);
	if (!*ssl) {
		sprintf(err_buf, "SSL ERR: Failed to init SSL");
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL); 
		return STIR_SHAKEN_STATUS_FALSE;
	}

	/* 1. Check if 256-bit eliptic curves are supported. */

	// Get total number of curves
	n = EC_get_builtin_curves(NULL, 0);
	if (n < 1) {
		sprintf(err_buf, "SSL ERR: Eliptic curves are not supported (change OpenSSL version to 1.0.2?)");
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL); 
		goto fail;
	}

	/* 2. Check support for "X9.62/SECG curve over a 256 bit prime field" */

	// Get curves description
	curves = malloc(n * sizeof(EC_builtin_curve));
	if (!curves) {
		sprintf(err_buf, "SSL ERR: Not enough memory");
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_GENERAL); 
		goto fail;
	}
	EC_get_builtin_curves(curves, n);

	// Search for "prime256v1" curve
	for (i = 0; i < n; ++i) {
		c = &curves[i];
		if (strstr(c->comment, "X9.62/SECG curve over a 256 bit prime field")) {
			curve_nid = c->nid;
			curve = c;
		}
	}

	if (curve_nid == -1 || !curve) {
		
		sprintf(err_buf, "SSL ERR: Eliptic curve 'X9.62/SECG curve over a 256 bit prime field' is not supported (change OpenSSL version to 1.0.2?)");
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_SSL); 
		goto fail;

	} else {
		
		// TODO remove
		printf("SSL: Using (%s [%d]) eliptic curve\n", curve->comment, curve->nid);
	}
	stir_shaken_globals.curve_nid = curve_nid;

#if USE_TN_AUTH_LIST_OID
	//stir_shaken_globals.tn_authlist_nid = OBJ_create(TN_AUTH_LIST_OID, TN_AUTH_LIST_SN, TN_AUTH_LIST_LN);
	stir_shaken_globals.tn_authlist_nid = OBJ_ln2nid(TN_AUTH_LIST_LN);
	if (stir_shaken_globals.tn_authlist_nid == NID_undef) {
		stir_shaken_set_error(ss, "SSL: Failed to create new openssl object for ext-tnAuthList extension", STIR_SHAKEN_ERROR_SSL);
		goto fail;
	}
#endif

	// TODO pass CAs list and revocation list
	if (STIR_SHAKEN_STATUS_OK != stir_shaken_init_cert_store(ss, NULL, ca_dir, NULL, crl_dir)) {
		sprintf(err_buf, "Cannot init x509 cert store (with: CA list: %s, CRL: %s", ca_dir ? ca_dir : "(null)", crl_dir ? crl_dir : "(null)");
		stir_shaken_set_error(ss, err_buf, STIR_SHAKEN_ERROR_CERT_STORE); 
		goto fail;
	}

	free(curves);
	curves = NULL;

	return STIR_SHAKEN_STATUS_OK;

fail:

	if (curves) {
		free(curves);
		curves = NULL;
	}
	stir_shaken_set_error_if_clear(ss, "Init SSL: Error", STIR_SHAKEN_ERROR_GENERAL);

	return STIR_SHAKEN_STATUS_FALSE;
}

void stir_shaken_deinit_ssl(void)
{
	SSL_CTX **ssl_ctx = &stir_shaken_globals.ssl_ctx;
	SSL     **ssl = &stir_shaken_globals.ssl;

	if (*ssl) {
		SSL_free(*ssl);
		*ssl = NULL;
	}

	if (*ssl_ctx) {
		SSL_CTX_free(*ssl_ctx);
		*ssl_ctx = NULL;
	}

	stir_shaken_cert_store_cleanup();
}
