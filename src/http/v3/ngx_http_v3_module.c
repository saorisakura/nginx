
/*
 * Copyright (C) Nginx, Inc.
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static ngx_int_t ngx_http_v3_variable_quic(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_v3_add_variables(ngx_conf_t *cf);
static void *ngx_http_v3_create_srv_conf(ngx_conf_t *cf);
static char *ngx_http_v3_merge_srv_conf(ngx_conf_t *cf, void *parent,
    void *child);
static char *ngx_http_quic_max_ack_delay(ngx_conf_t *cf, void *post,
    void *data);
static char *ngx_http_quic_max_udp_payload_size(ngx_conf_t *cf, void *post,
    void *data);
static char *ngx_http_quic_host_key(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_http_v3_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_v3_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static char *ngx_http_v3_push(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);


static ngx_conf_post_t  ngx_http_quic_max_ack_delay_post =
    { ngx_http_quic_max_ack_delay };
static ngx_conf_post_t  ngx_http_quic_max_udp_payload_size_post =
    { ngx_http_quic_max_udp_payload_size };
static ngx_conf_num_bounds_t  ngx_http_quic_ack_delay_exponent_bounds =
    { ngx_conf_check_num_bounds, 0, 20 };
static ngx_conf_num_bounds_t  ngx_http_quic_active_connection_id_limit_bounds =
    { ngx_conf_check_num_bounds, 2, -1 };


static ngx_command_t  ngx_http_v3_commands[] = {

    { ngx_string("http3_max_table_capacity"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, max_table_capacity),
      NULL },

    { ngx_string("http3_max_blocked_streams"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, max_blocked_streams),
      NULL },

    { ngx_string("http3_max_concurrent_pushes"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, max_concurrent_pushes),
      NULL },

    { ngx_string("http3_max_uni_streams"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, max_uni_streams),
      NULL },

    { ngx_string("http3_push"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_v3_push,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("http3_push_preload"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_v3_loc_conf_t, push_preload),
      NULL },

    { ngx_string("quic_max_idle_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, quic.tp.max_idle_timeout),
      NULL },

    { ngx_string("quic_max_ack_delay"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, quic.tp.max_ack_delay),
      &ngx_http_quic_max_ack_delay_post },

    { ngx_string("quic_max_udp_payload_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, quic.tp.max_udp_payload_size),
      &ngx_http_quic_max_udp_payload_size_post },

    { ngx_string("quic_initial_max_data"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, quic.tp.initial_max_data),
      NULL },

    { ngx_string("quic_initial_max_stream_data_bidi_local"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t,
               quic.tp.initial_max_stream_data_bidi_local),
      NULL },

    { ngx_string("quic_initial_max_stream_data_bidi_remote"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t,
               quic.tp.initial_max_stream_data_bidi_remote),
      NULL },

    { ngx_string("quic_initial_max_stream_data_uni"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, quic.tp.initial_max_stream_data_uni),
      NULL },

    { ngx_string("quic_initial_max_streams_bidi"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, quic.tp.initial_max_streams_bidi),
      NULL },

    { ngx_string("quic_initial_max_streams_uni"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, quic.tp.initial_max_streams_uni),
      NULL },

    { ngx_string("quic_ack_delay_exponent"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, quic.tp.ack_delay_exponent),
      &ngx_http_quic_ack_delay_exponent_bounds },

    { ngx_string("quic_disable_active_migration"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_flag_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, quic.tp.disable_active_migration),
      NULL },

    { ngx_string("quic_active_connection_id_limit"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, quic.tp.active_connection_id_limit),
      &ngx_http_quic_active_connection_id_limit_bounds },

    { ngx_string("quic_retry"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, quic.retry),
      NULL },

    { ngx_string("quic_gso"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_SRV_CONF_OFFSET,
      offsetof(ngx_http_v3_srv_conf_t, quic.gso_enabled),
      NULL },

    { ngx_string("quic_host_key"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_CONF_TAKE1,
      ngx_http_quic_host_key,
      NGX_HTTP_SRV_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_v3_module_ctx = {
    ngx_http_v3_add_variables,             /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    ngx_http_v3_create_srv_conf,           /* create server configuration */
    ngx_http_v3_merge_srv_conf,            /* merge server configuration */

    ngx_http_v3_create_loc_conf,           /* create location configuration */
    ngx_http_v3_merge_loc_conf             /* merge location configuration */
};


ngx_module_t  ngx_http_v3_module = {
    NGX_MODULE_V1,
    &ngx_http_v3_module_ctx,               /* module context */
    ngx_http_v3_commands,                  /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_variable_t  ngx_http_v3_vars[] = {

    { ngx_string("quic"), NULL, ngx_http_v3_variable_quic, 0, 0, 0 },

      ngx_http_null_variable
};

static ngx_str_t  ngx_http_quic_salt = ngx_string("ngx_quic");


static ngx_int_t
ngx_http_v3_variable_quic(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    if (r->connection->quic) {

        v->len = 4;
        v->valid = 1;
        v->no_cacheable = 1;
        v->not_found = 0;
        v->data = (u_char *) "quic";
        return NGX_OK;
    }

    v->not_found = 1;

    return NGX_OK;
}


static ngx_int_t
ngx_http_v3_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t  *var, *v;

    for (v = ngx_http_v3_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static void *
ngx_http_v3_create_srv_conf(ngx_conf_t *cf)
{
    ngx_http_v3_srv_conf_t  *h3scf;

    h3scf = ngx_pcalloc(cf->pool, sizeof(ngx_http_v3_srv_conf_t));
    if (h3scf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     h3scf->quic.tp.original_dcid = { 0, NULL };
     *     h3scf->quic.tp.initial_scid = { 0, NULL };
     *     h3scf->quic.tp.retry_scid = { 0, NULL };
     *     h3scf->quic.tp.sr_token = { 0 }
     *     h3scf->quic.tp.sr_enabled = 0
     *     h3scf->quic.tp.preferred_address = NULL
     *     h3scf->quic.host_key = { 0, NULL }
     *     h3scf->quic.stream_reject_code_uni = 0;
     */

    h3scf->max_table_capacity = NGX_CONF_UNSET_SIZE;
    h3scf->max_blocked_streams = NGX_CONF_UNSET_UINT;
    h3scf->max_concurrent_pushes = NGX_CONF_UNSET_UINT;
    h3scf->max_uni_streams = NGX_CONF_UNSET_UINT;

    h3scf->quic.tp.max_idle_timeout = NGX_CONF_UNSET_MSEC;
    h3scf->quic.tp.max_ack_delay = NGX_CONF_UNSET_MSEC;
    h3scf->quic.tp.max_udp_payload_size = NGX_CONF_UNSET_SIZE;
    h3scf->quic.tp.initial_max_data = NGX_CONF_UNSET_SIZE;
    h3scf->quic.tp.initial_max_stream_data_bidi_local = NGX_CONF_UNSET_SIZE;
    h3scf->quic.tp.initial_max_stream_data_bidi_remote = NGX_CONF_UNSET_SIZE;
    h3scf->quic.tp.initial_max_stream_data_uni = NGX_CONF_UNSET_SIZE;
    h3scf->quic.tp.initial_max_streams_bidi = NGX_CONF_UNSET_UINT;
    h3scf->quic.tp.initial_max_streams_uni = NGX_CONF_UNSET_UINT;
    h3scf->quic.tp.ack_delay_exponent = NGX_CONF_UNSET_UINT;
    h3scf->quic.tp.disable_active_migration = NGX_CONF_UNSET;
    h3scf->quic.tp.active_connection_id_limit = NGX_CONF_UNSET_UINT;

    h3scf->quic.retry = NGX_CONF_UNSET;
    h3scf->quic.gso_enabled = NGX_CONF_UNSET;
    h3scf->quic.stream_close_code = NGX_HTTP_V3_ERR_NO_ERROR;
    h3scf->quic.stream_reject_code_bidi = NGX_HTTP_V3_ERR_REQUEST_REJECTED;

    return h3scf;
}


static char *
ngx_http_v3_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_v3_srv_conf_t *prev = parent;
    ngx_http_v3_srv_conf_t *conf = child;

    ngx_http_ssl_srv_conf_t  *sscf;

    ngx_conf_merge_size_value(conf->max_table_capacity,
                              prev->max_table_capacity, 16384);

    ngx_conf_merge_uint_value(conf->max_blocked_streams,
                              prev->max_blocked_streams, 16);

    ngx_conf_merge_uint_value(conf->max_concurrent_pushes,
                              prev->max_concurrent_pushes, 10);

    ngx_conf_merge_uint_value(conf->max_uni_streams,
                              prev->max_uni_streams, 3);

    ngx_conf_merge_msec_value(conf->quic.tp.max_idle_timeout,
                              prev->quic.tp.max_idle_timeout, 60000);

    ngx_conf_merge_msec_value(conf->quic.tp.max_ack_delay,
                              prev->quic.tp.max_ack_delay,
                              NGX_QUIC_DEFAULT_MAX_ACK_DELAY);

    ngx_conf_merge_size_value(conf->quic.tp.max_udp_payload_size,
                              prev->quic.tp.max_udp_payload_size,
                              NGX_QUIC_MAX_UDP_PAYLOAD_SIZE);

    ngx_conf_merge_size_value(conf->quic.tp.initial_max_data,
                              prev->quic.tp.initial_max_data,
                              16 * NGX_QUIC_STREAM_BUFSIZE);

    ngx_conf_merge_size_value(conf->quic.tp.initial_max_stream_data_bidi_local,
                              prev->quic.tp.initial_max_stream_data_bidi_local,
                              NGX_QUIC_STREAM_BUFSIZE);

    ngx_conf_merge_size_value(conf->quic.tp.initial_max_stream_data_bidi_remote,
                              prev->quic.tp.initial_max_stream_data_bidi_remote,
                              NGX_QUIC_STREAM_BUFSIZE);

    ngx_conf_merge_size_value(conf->quic.tp.initial_max_stream_data_uni,
                              prev->quic.tp.initial_max_stream_data_uni,
                              NGX_QUIC_STREAM_BUFSIZE);

    ngx_conf_merge_uint_value(conf->quic.tp.initial_max_streams_bidi,
                              prev->quic.tp.initial_max_streams_bidi, 16);

    ngx_conf_merge_uint_value(conf->quic.tp.initial_max_streams_uni,
                              prev->quic.tp.initial_max_streams_uni, 3);

    ngx_conf_merge_uint_value(conf->quic.tp.ack_delay_exponent,
                              prev->quic.tp.ack_delay_exponent,
                              NGX_QUIC_DEFAULT_ACK_DELAY_EXPONENT);

    ngx_conf_merge_value(conf->quic.tp.disable_active_migration,
                              prev->quic.tp.disable_active_migration, 0);

    ngx_conf_merge_uint_value(conf->quic.tp.active_connection_id_limit,
                              prev->quic.tp.active_connection_id_limit, 2);

    ngx_conf_merge_value(conf->quic.retry, prev->quic.retry, 0);
    ngx_conf_merge_value(conf->quic.gso_enabled, prev->quic.gso_enabled, 0);

    ngx_conf_merge_str_value(conf->quic.host_key, prev->quic.host_key, "");

    if (conf->quic.host_key.len == 0) {

        conf->quic.host_key.len = NGX_QUIC_DEFAULT_HOST_KEY_LEN;
        conf->quic.host_key.data = ngx_palloc(cf->pool,
                                              conf->quic.host_key.len);
        if (conf->quic.host_key.data == NULL) {
            return NGX_CONF_ERROR;
        }

        if (RAND_bytes(conf->quic.host_key.data, NGX_QUIC_DEFAULT_HOST_KEY_LEN)
            <= 0)
        {
            return NGX_CONF_ERROR;
        }
    }

    if (ngx_quic_derive_key(cf->log, "av_token_key",
                            &conf->quic.host_key, &ngx_http_quic_salt,
                            conf->quic.av_token_key, NGX_QUIC_AV_KEY_LEN)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    if (ngx_quic_derive_key(cf->log, "sr_token_key",
                            &conf->quic.host_key, &ngx_http_quic_salt,
                            conf->quic.sr_token_key, NGX_QUIC_SR_KEY_LEN)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    sscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_ssl_module);
    conf->quic.ssl = &sscf->ssl;

    return NGX_CONF_OK;
}


static char *
ngx_http_quic_max_ack_delay(ngx_conf_t *cf, void *post, void *data)
{
    ngx_msec_t *sp = data;

    if (*sp >= 16384) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"quic_max_ack_delay\" must be less than 16384");

        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_quic_max_udp_payload_size(ngx_conf_t *cf, void *post, void *data)
{
    size_t *sp = data;

    if (*sp < NGX_QUIC_MIN_INITIAL_SIZE
        || *sp > NGX_QUIC_MAX_UDP_PAYLOAD_SIZE)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"quic_max_udp_payload_size\" must be between "
                           "%d and %d",
                           NGX_QUIC_MIN_INITIAL_SIZE,
                           NGX_QUIC_MAX_UDP_PAYLOAD_SIZE);

        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_quic_host_key(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_v3_srv_conf_t  *h3scf = conf;

    u_char           *buf;
    size_t            size;
    ssize_t           n;
    ngx_str_t        *value;
    ngx_file_t        file;
    ngx_file_info_t   fi;
    ngx_quic_conf_t  *qcf;

    qcf = &h3scf->quic;

    if (qcf->host_key.len) {
        return "is duplicate";
    }

    buf = NULL;
#if (NGX_SUPPRESS_WARN)
    size = 0;
#endif

    value = cf->args->elts;

    if (ngx_conf_full_name(cf->cycle, &value[1], 1) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&file, sizeof(ngx_file_t));
    file.name = value[1];
    file.log = cf->log;

    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);

    if (file.fd == NGX_INVALID_FILE) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           ngx_open_file_n " \"%V\" failed", &file.name);
        return NGX_CONF_ERROR;
    }

    if (ngx_fd_info(file.fd, &fi) == NGX_FILE_ERROR) {
        ngx_conf_log_error(NGX_LOG_CRIT, cf, ngx_errno,
                           ngx_fd_info_n " \"%V\" failed", &file.name);
        goto failed;
    }

    size = ngx_file_size(&fi);

    if (size == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"%V\" zero key size", &file.name);
        goto failed;
    }

    buf = ngx_pnalloc(cf->pool, size);
    if (buf == NULL) {
        goto failed;
    }

    n = ngx_read_file(&file, buf, size, 0);

    if (n == NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_CRIT, cf, ngx_errno,
                           ngx_read_file_n " \"%V\" failed", &file.name);
        goto failed;
    }

    if ((size_t) n != size) {
        ngx_conf_log_error(NGX_LOG_CRIT, cf, 0,
                           ngx_read_file_n " \"%V\" returned only "
                           "%z bytes instead of %uz", &file.name, n, size);
        goto failed;
    }

    qcf->host_key.data = buf;
    qcf->host_key.len = n;

    if (ngx_close_file(file.fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, cf->log, ngx_errno,
                      ngx_close_file_n " \"%V\" failed", &file.name);
    }

    return NGX_CONF_OK;

failed:

    if (ngx_close_file(file.fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, cf->log, ngx_errno,
                      ngx_close_file_n " \"%V\" failed", &file.name);
    }

    if (buf) {
        ngx_explicit_memzero(buf, size);
    }

    return NGX_CONF_ERROR;
}


static void *
ngx_http_v3_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_v3_loc_conf_t  *h3lcf;

    h3lcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_v3_loc_conf_t));
    if (h3lcf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     h3lcf->pushes = NULL;
     */

    h3lcf->push_preload = NGX_CONF_UNSET;
    h3lcf->push = NGX_CONF_UNSET;

    return h3lcf;
}


static char *
ngx_http_v3_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_v3_loc_conf_t *prev = parent;
    ngx_http_v3_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->push, prev->push, 1);

    if (conf->push && conf->pushes == NULL) {
        conf->pushes = prev->pushes;
    }

    ngx_conf_merge_value(conf->push_preload, prev->push_preload, 0);

    return NGX_CONF_OK;
}


static char *
ngx_http_v3_push(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_v3_loc_conf_t *h3lcf = conf;

    ngx_str_t                         *value;
    ngx_http_complex_value_t          *cv;
    ngx_http_compile_complex_value_t   ccv;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "off") == 0) {

        if (h3lcf->pushes) {
            return "\"off\" parameter cannot be used with URI";
        }

        if (h3lcf->push == 0) {
            return "is duplicate";
        }

        h3lcf->push = 0;
        return NGX_CONF_OK;
    }

    if (h3lcf->push == 0) {
        return "URI cannot be used with \"off\" parameter";
    }

    h3lcf->push = 1;

    if (h3lcf->pushes == NULL) {
        h3lcf->pushes = ngx_array_create(cf->pool, 1,
                                         sizeof(ngx_http_complex_value_t));
        if (h3lcf->pushes == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    cv = ngx_array_push(h3lcf->pushes);
    if (cv == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = &value[1];
    ccv.complex_value = cv;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}
