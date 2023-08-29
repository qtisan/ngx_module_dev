
/*
 * Copyright (C) Silo QIAN.
 * ngx_http_save_ctx_2store_module.c
 *
 * This module is used to save the context of a request to a database.
 *
 * ## Configuration example:
 *
 * ```
 * server {
 *     listen       80;
 *     server_name  localhost;
 *
 *     # Set the connection string to the database
 *     save_ctx_2store_conn mysql://root:root@localhost:3306/ctx_2store;
 *
 *     location / {
 *
 *         # Enable the context saving
 *         save_ctx_2store;
 *
 *         proxy_pass http://real.api;
 *         proxy_http_version 1.1;
 *         proxy_set_header Connection "keep-alive";
 *     }
 * }
 * ```
 *
 * ## Workflow:
 *
 * 1. Initialize the connection to the database when nginx starts.
 *
 *     - If the connection fails, the module will not work, see the log for more information.
 *     - If the connection is successful, the module will save the connection instance pointer.
 *
 *     **Details:**
 *
 *     1.1 `ngx_http_save_ctx_2store_create_srv_conf` is called for a space storing `conn_str`
 *         when nginx starts, then merge, error when not indicate.
 *     1.2 Take a space from memory for `ngx_store_conf_t` struct, by nginx commands parse, with
 *         `ngx_http_save_ctx_2store_set_conn_str` configuration command.
 *     1.3 While merging the configuration, `ngx_http_save_ctx_2store_merge_srv_conf` is called,
 *         the initial `conn_str` is set, default with `NO_CONNECTION_STRING`.
 *     1.4 Database connection is initialized in `store_connect()`, and save the connection instance
 *         pointer to the `conn_ptr` field of the `ngx_store_conf_t` struct.
 *
 * 2. When a request comes, save the context of the request to the database.
 *     - If the context saving is disabled, the module will not work.
 *     - If the context saving is enabled, the module will save the context of the request to the
 *       database.
 *
 *     **Details:**
 *
 *
 * ## Supported database:
 *
 * - MySQL
 *
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#define NO_CONNECTION_STRING "No connection string"
#define NO_BODY "No body"
#define check_alloc(p, log, result, f)                        \
    if (p == NULL)                                            \
    {                                                         \
        ngx_log_error(NGX_LOG_EMERG, log, 0,                  \
                      "[x] Memory alloc failed, in [%s]", f); \
        return result;                                        \
    }

/**
 * Configuration at the `server` segment
 * Set the connection string to the database
 *
 * ## Usage
 *
 */
typedef struct
{
    /* connection string */
    ngx_str_t conn_str;
    /* TODO: replaced by connection instance ptr type */
    ngx_uint_t conn_ptr;
} ngx_store_conf_t;

/**
 * Configuration at the `location` segment
 * Set if enable the context saving
 *
 * See the struct `ngx_store_conf_t` for an example.
 */
typedef struct
{
    /* on | off */
    ngx_flag_t enable;
} ngx_store_enable_conf_t;

typedef struct
{
    ngx_str_t referer;
    ngx_str_t method;
    ngx_str_t uri;
    ngx_str_t auth;
    ngx_str_t user_agent;
    ngx_str_t remote_addr;
    ngx_str_t query;
    ngx_str_t request_body;
    ngx_uint_t status;
    ngx_uint_t response_time;
} ngx_store_data_t;

static ngx_uint_t
length_of_store_data(ngx_store_data_t *data)
{
    ngx_uint_t length = 0;
    length += data->referer.len;
    length += data->method.len;
    length += data->uri.len;
    length += data->auth.len;
    length += data->user_agent.len;
    length += data->remote_addr.len;
    length += data->query.len;
    length += data->request_body.len;
    length += sizeof(data->status);
    length += sizeof(data->response_time);
    return length;
}

static void *ngx_http_save_ctx_2store_create_srv_conf(ngx_conf_t *cf);
static char *ngx_http_save_ctx_2store_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child);
static void *ngx_http_save_ctx_2store_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_save_ctx_2store_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
/**
 * Connect to a store.
 */
static char *store_connect(ngx_conf_t *cf, void *data, void *conf);
/**
 * Convert any pointer to `ngx_store_conf_t *`.
 */
static ngx_store_conf_t *convert_to_store_conf(ngx_conf_t *cf, void *conf);
/**
 * Convert any pointer to `ngx_store_enable_conf_t *`.
 */
static ngx_store_enable_conf_t *convert_to_store_enable_conf(ngx_log_t *log, void *conf);
/**
 * Set the connection string when parsing the `server` segment of the conf file.
 */
static char *ngx_http_save_ctx_2store_set_conn_str(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_conf_post_t ngx_http_save_ctx_2store_post_set_conn_str = {store_connect};
/**
 * Running when a request comes, save the context of the request to the database.
 */
static ngx_int_t ngx_http_append_body_filter(ngx_http_request_t *r, ngx_chain_t *in);
/**
 * Running when nginx starts, initialize, and bind handler to the http process.
 */
static ngx_int_t ngx_http_save_ctx_2store_init(ngx_conf_t *cf);

/* next body filters in chain */
static ngx_http_output_body_filter_pt ngx_http_next_body_filter;

/**
 * Settings in conf file
 * Will running while nginx start, parsing nginx.conf file
 *
 * **Official**: The `ngx_command_t` type defines a single configuration directive.
 * Each module that supports configuration provides an array of such structures
 * that describe how to process arguments and what handlers to call
 */
static ngx_command_t
    save_ctx_2store_commands[] = {

        {/* Command name */
         ngx_string("save_ctx_2store_conn"),
         /* In the `server` segment, take 1 arguments: conn_str */
         NGX_HTTP_SRV_CONF | NGX_CONF_TAKE1,
         /* Set the conn_str */
         ngx_http_save_ctx_2store_set_conn_str,
         NGX_HTTP_SRV_CONF_OFFSET,
         0,
         /* Connect to a store */
         &ngx_http_save_ctx_2store_post_set_conn_str},

        {ngx_string("save_ctx_2store"),
         NGX_HTTP_LOC_CONF | NGX_CONF_TAKE1,
         ngx_conf_set_flag_slot,
         NGX_HTTP_LOC_CONF_OFFSET,
         offsetof(ngx_store_enable_conf_t, enable),
         NULL},

        ngx_null_command};

static ngx_store_conf_t *
convert_to_store_conf(ngx_conf_t *cf, void *conf)
{
    ngx_store_conf_t *store_conf = conf;
    if (store_conf == NGX_CONF_UNSET_PTR || store_conf == NULL)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "[x] Connot parse the connection string, or null at: %p", (void *)store_conf);
        return NULL;
    }
    return store_conf;
}

static ngx_store_enable_conf_t *
convert_to_store_enable_conf(ngx_log_t *log, void *conf)
{
    ngx_store_enable_conf_t *store_enable_conf = conf;
    if (store_enable_conf == NGX_CONF_UNSET_PTR || store_enable_conf == NULL)
    {
        ngx_log_error(NGX_LOG_EMERG, log, 0,
                      "[x] Connot parse the store_enable_conf, or null at: %p", (void *)store_enable_conf);
        return NULL;
    }
    return store_enable_conf;
}

static char *
ngx_http_save_ctx_2store_set_conn_str(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t *value;
    ngx_conf_post_t *post;
    ngx_store_conf_t *store_conf = convert_to_store_conf(cf, conf);
    value = cf->args->elts;
    store_conf->conn_str = value[1];

    if (store_conf->conn_str.len > 0)
    {
        ngx_log_debug1(NGX_LOG_DEBUG, cf->log, 0,
                       "[*] connection string: [%s], in ngx_http_save_ctx_2store_set_conn_str.", store_conf->conn_str.data);
    }

    if (cmd->post)
    {
        post = cmd->post;
        return post->post_handler(cf, value, store_conf);
    }

    return NGX_CONF_OK;
}

static char *
store_connect(ngx_conf_t *cf, void *data, void *conf)
{
    ngx_store_conf_t *store_conf = convert_to_store_conf(cf, conf);
    ngx_log_debug1(NGX_LOG_DEBUG, cf->log, 0,
                   "[*] Connecting to store in [store_connect]: %s.", store_conf->conn_str.data);
    if (ngx_strcmp(store_conf->conn_str.data, NO_CONNECTION_STRING) == 0)
    {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "[x] No connection string set.");
        return NGX_CONF_ERROR;
    }
    /* TODO: Connect to a db, set the connection instance. */
    store_conf->conn_ptr = 109;
    return NGX_CONF_OK;
}

static ngx_store_data_t *
compose_request_ctx(ngx_http_request_t *r)
{
    ngx_store_data_t *store_data;
    ngx_buf_t *b = NULL;
    ngx_str_t body = ngx_string(NO_BODY);
    ngx_str_t empty = ngx_string("");

    store_data = ngx_pcalloc(r->pool, sizeof(ngx_store_data_t));
    check_alloc(store_data, r->connection->log, NULL, "compose_request_ctx(store_data)");
    if (r->request_body != NULL && r->request_body->bufs != NULL)
    {
        b = r->request_body->bufs->buf;
    }
    if (b == NULL)
    {
        store_data->request_body.data = (u_char *)ngx_pstrdup(r->pool, &body);
        store_data->request_body.len = ngx_strlen(NO_BODY);
    }
    else
    {
        store_data->request_body.data = b->pos;
        store_data->request_body.len = b->last - b->pos;
    }
    if (r->headers_in.referer != NULL)
    {
        store_data->referer = r->headers_in.referer->value;
    }
    else
    {
        store_data->referer = empty;
    }
    store_data->method = r->method_name;
    store_data->uri = r->uri;

    if (r->headers_in.authorization != NULL)
    {
        store_data->auth = r->headers_in.authorization->value;
    }
    else
    {
        store_data->auth = empty;
    }
    if (r->headers_in.user_agent != NULL)
    {
        store_data->user_agent = r->headers_in.user_agent->value;
    }
    else
    {
        store_data->user_agent = empty;
    }

    store_data->remote_addr = r->connection->addr_text;
    store_data->query = r->args;

    return store_data;
}

static ngx_int_t
ngx_http_append_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "[*] Running ngx_http_ctx_save_2store_handler.");
    ngx_store_enable_conf_t *enable_conf;
    ngx_store_data_t *store_data;
    ngx_uint_t store_data_len;
    ngx_int_t rc;
    ngx_buf_t *b;
    ngx_chain_t out;

    enable_conf = convert_to_store_enable_conf(r->connection->log, r->loc_conf);
    if (enable_conf->enable)
    {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "[*] Store is enabled, running save context to store...");

        store_data = compose_request_ctx(r);
        store_data_len = length_of_store_data(store_data) + 500;
        check_alloc(store_data, r->connection->log, NGX_ERROR, "ngx_http_ctx_save_2store_handler(store_data)");
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "[*] Compose request context success.");

        rc = ngx_http_next_body_filter(r, in);

        if (rc == NGX_ERROR)
        {
            return rc;
        }
        /* send body */
        b = ngx_create_temp_buf(r->pool, store_data_len);
        check_alloc(b, r->connection->log, NGX_ERROR, "ngx_http_ctx_save_2store_handler(b)");

        b->last = ngx_sprintf(b->pos,
                              "UPDATE table_t SET referer='%s', method='%s', uri='%s', auth='%s', user_agent='%s', remote_addr='%s', query='%s', request_body='%s', status=%d, response_time=%d;",
                              store_data->referer.data,
                              store_data->method.data,
                              store_data->uri.data,
                              store_data->auth.data,
                              store_data->user_agent.data,
                              store_data->remote_addr.data,
                              store_data->query.data,
                              store_data->request_body.data,
                              store_data->status,
                              store_data->response_time);
        b->memory = 1;
        b->last_buf = (r == r->main) ? 1 : 0;
        b->last_in_chain = 1;

        out.buf = b;
        out.next = NULL;

        ngx_log_error(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                      "[*] Compose json success: %s", b->pos);

        /* TODO: Save data to db */
    }
    else
    {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "[*] Store is disabled, skip save context to store.");
    }
    return ngx_http_next_body_filter(r, &out);
}

static void *
ngx_http_save_ctx_2store_create_srv_conf(ngx_conf_t *cf)
{
    ngx_store_conf_t *conf;
    if (ngx_exiting)
    {
        return NULL;
    }
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_store_conf_t));
    /* force enable debug */
    cf->log->log_level = NGX_LOG_DEBUG;
    check_alloc(conf, cf->log, NULL, "ngx_http_save_ctx_2store_create_srv_conf(conf)");
    conf->conn_ptr = NGX_CONF_UNSET_UINT;
    return conf;
}

static char *
ngx_http_save_ctx_2store_merge_srv_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_store_conf_t *prev = parent;
    ngx_store_conf_t *conf = child;
    if (ngx_exiting)
    {
        return NGX_CONF_OK;
    }
    ngx_conf_merge_str_value(conf->conn_str, prev->conn_str, NO_CONNECTION_STRING);
    ngx_conf_merge_uint_value(conf->conn_ptr, prev->conn_ptr, NGX_CONF_UNSET_UINT);
    return NGX_CONF_OK;
}

static void *
ngx_http_save_ctx_2store_create_loc_conf(ngx_conf_t *cf)
{
    ngx_store_enable_conf_t *conf;
    if (ngx_exiting)
    {
        return NULL;
    }
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_store_enable_conf_t));
    check_alloc(conf, cf->log, NULL, "ngx_http_save_ctx_2store_create_loc_conf(conf)");
    conf->enable = NGX_CONF_UNSET;
    return conf;
}

static char *
ngx_http_save_ctx_2store_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_store_enable_conf_t *prev = parent;
    ngx_store_enable_conf_t *conf = child;
    if (ngx_exiting)
    {
        return NGX_CONF_OK;
    }
    ngx_conf_merge_value(conf->enable, prev->enable, NGX_CONF_UNSET);
    return NGX_CONF_OK;
}

static ngx_int_t
ngx_http_save_ctx_2store_init(ngx_conf_t *cf)
{
    /* install handler in body filter chain */

    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_append_body_filter;

    return NGX_OK;
}

/**
 * Hook functions
 * See the `ngx_http_module_t` in `ngx_http_save_ctx_2store_module_ctx`
 * for the hook functions details
 * At the configuration life cycle, the hook functions will be called
 */
static ngx_http_module_t
    ngx_http_save_ctx_2store_module_ctx = {
        NULL,                                     /* preconfiguration */
        ngx_http_save_ctx_2store_init,            /* postconfiguration */
        NULL,                                     /* create main configuration */
        NULL,                                     /* init main configuration */
        ngx_http_save_ctx_2store_create_srv_conf, /* create server configuration */
        ngx_http_save_ctx_2store_merge_srv_conf,  /* merge server configuration */
        ngx_http_save_ctx_2store_create_loc_conf, /* create location configuration */
        ngx_http_save_ctx_2store_merge_loc_conf   /* merge location configuration */
};

/**
 * Module definition
 */
ngx_module_t
    ngx_http_save_ctx_2store_module = {
        NGX_MODULE_V1,
        &ngx_http_save_ctx_2store_module_ctx,
        save_ctx_2store_commands,
        NGX_HTTP_MODULE,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NGX_MODULE_V1_PADDING};
