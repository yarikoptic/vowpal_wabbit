#include "api_status.h"
#include "config_collection.h"
#include "error_callback_fn.h"
#include "logger/logger.h"
#include "ranking_response.h"
#include "live_model_impl.h"
#include "ranking_event.h"
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>
#include "err_constants.h"

using namespace std;

// Why while(0) ? It make the macro safe under various conditions. Check link below
// https://stackoverflow.com/questions/257418/do-while-0-what-is-it-good-for

namespace reinforcement_learning
{
  int check_null_or_empty(const char* arg1, const char* arg2, api_status* status);

  int live_model_impl::init(api_status* status) {
    int scode = _logger.init(status);
    TRY_OR_RETURN(scode);
    //scode = init_model_mgmt(status);
    return scode;
  }

  int live_model_impl::choose_rank(const char* uuid, const char* context, ranking_response& response, api_status* status) 
  {
    //clear previous errors if any
    api_status::try_clear(status);

    //check arguments
    TRY_OR_RETURN(check_null_or_empty(uuid, context, status));

    /* GET ACTIONS PROBABILITIES FROM VW */

    /**/ // TODO: replace with call to parse example and predict
    /**/ // TODO: once that is complete
    response.push_back(2, 0.4f);
    response.push_back(1, 0.3f);
    response.push_back(4, 0.2f);
    response.push_back(3, 0.1f);
      
    const string model_id = "model_id";
      
    //send the ranking event to the backend
    _buff.seekp(0, _buff.beg);
    ranking_event::serialize(_buff, uuid, context, response, model_id);
    auto sbuf = _buff.str();
    TRY_OR_RETURN(_logger.append_ranking(sbuf, status));

    response.set_uuid(uuid);
    return error_code::success;
  }

  //here the uuid is auto-generated
  int live_model_impl::choose_rank(const char* context, ranking_response& response, api_status* status) {
    return choose_rank(context, boost::uuids::to_string(boost::uuids::random_generator()( )).c_str(), response,
      status);
  }

  int live_model_impl::report_outcome(const char* uuid, const char* outcome_data, api_status* status) {
    //clear previous errors if any
    api_status::try_clear(status);

    //check arguments
    TRY_OR_RETURN(check_null_or_empty(uuid, outcome_data, status));

    //send the outcome event to the backend
    _buff.seekp(0, _buff.beg);
    outcome_event::serialize(_buff,uuid, outcome_data);
    auto sbuf = _buff.str();
    TRY_OR_RETURN(_logger.append_outcome(sbuf, status));

    return error_code::success;
  }

  int live_model_impl::report_outcome(const char* uuid, float reward, api_status* status) {
    return report_outcome(uuid, to_string(reward).c_str(), status);
  }

  live_model_impl::live_model_impl(const utility::config_collection& config, error_fn fn, void* err_context)
      : _configuration(config),
      _error_cb(fn, err_context),
      _logger(config, &_error_cb) {
  }

  int live_model_impl::init_model_mgmt(api_status* status) {
    throw "not yet implemented";
  }
}
