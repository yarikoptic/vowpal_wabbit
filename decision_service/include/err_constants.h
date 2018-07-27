#pragma once

namespace reinforcement_learning { namespace error_code {
  //success code
  const int success = 0;

  //error code
  const int invalid_argument            = 1;
  const int background_queue_overflow   = 2;
  const int eventhub_http_generic       = 3;
  const int http_bad_status_code        = 4;
  const int action_not_found            = 5;
  const int background_thread_start     = 6;
  const int not_initialized             = 7;
  const int eventhub_generate_SAS_hash  = 8;
  const int create_fn_exception         = 9;
  const int type_not_registered         = 10;
  const int http_uri_not_provided       = 11;
  const int last_modified_not_found     = 12;
  const int last_modified_invalid       = 13;
  const int bad_content_length          = 14;
  const int exception_during_http_req   = 15;
  const int model_export_frequency_not_provided = 16;
  const int bad_time_interval           = 17;
  const int data_callback_exception     = 18;
  const int data_callback_not_set       = 19;
}}

namespace reinforcement_learning { namespace error_code {
  //error message
  const char * const unkown_s                   = "Unexpected error.";
  const char * const create_fn_exception_s      = "Create function failed.";
  const char * const type_not_registered_s      = "Type not registered with class factory";
  const char * const http_uri_not_provided_s    = "URL parameter was not passed in via config_collection";
  const char * const http_bad_status_code_s     = "http request returned a bad status code";
  const char * const last_modified_not_found_s  = "Last-Modified http header not found in response";
  const char * const last_modified_invalid_s    = "Unable to parse Last-Modified http header as date-time";
  const char * const bad_content_length_s       = "Content-Length header not set or set to zero";
  const char * const model_export_frequency_not_provided_s = "Export frequency of model not specified in configuration.";
  const char * const bad_time_interval_s        = "Bad time interval string.  Format should be hh:mm:ss";
  const char * const data_callback_exception_s  = "Background data callback threw an exception. ";
  const char * const data_callback_not_set_s    = "Data callback handler not set";
}}
