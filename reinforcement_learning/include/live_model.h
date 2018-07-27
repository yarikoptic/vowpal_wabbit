/**
 * @brief RL Inference API definition.
 * 
 * @file live_model.h
 * @author Rajan Chari et al
 * @date 2018-07-18
 */
#pragma once
#include "ranking_response.h"
#include "err_constants.h"
#include "factory_resolver.h"

namespace reinforcement_learning {

  //// Forward declarations ////////
  class live_model_impl;          //
  class ranking_response;         //
  class api_status;               //
                                  //
  namespace model_management {    //
    class i_data_transport;       //
    class i_model;                //
  }                               //
                                  //
  namespace utility {             //
    class config_collection;      //
  }                               //
  //////////////////////////////////

	// Reinforcement learning client
  /**
   * @brief Interface class for the Inference API.   
   * 
   * - (1) Instantiate and Initialize 
   * - (2) choose_rank() to choose an action from a list of actions
   * - (3) report_outcome() to provide feedback on choosen action  
   */
	class live_model {

	public:
    /**
     * @brief Error callback function.  
     * When live_model is constructed, a background error callback and a 
     * context (void*) is registered. If there is an error in the background thread, 
     * error callback will get invoked with api_status and the context (void*).
     * 
     * NOTE: Error callback will get invoked in a background thread. 
     */
    using error_fn            = void(*)(const api_status&, void*);
    /**
     * @brief Factory to create transport for model data.  
     * Advanced extension point:  Register another implementation of i_data_transport to 
     * provide updated model data used to hydrate inference model.
     */
    using transport_factory_t = utility::object_factory<model_management::i_data_transport>;
    /**
     * @brief Factory to create model used in inference.  
     * Advanced extension point:  Register another implementation of i_model to 
     * provide hydraded model given updated model data. This model is then used 
     * in inference.
     */
    using model_factory_t     = utility::object_factory<model_management::i_model>;

    /**
     * @brief Construct a new live model object.  
     * 
     * @param config Name-Value based configuration
     * @param fn Error callback for handling errors in background thread
     * @param err_context Context passed back during Error callback
     * @param t_factory Transport factory.  The default transport factory is initialised with a 
     *                  REST based transport that gets data  from an Azure storage account
     * @param m_factory Model factory.  The default model factory hydrates vw models 
     *                    used for local inference.
     */
    explicit live_model(
      const utility::config_collection& config, 
      error_fn fn = nullptr,                     
      void* err_context = nullptr,               
      transport_factory_t* t_factory = &data_transport_factory, 
      model_factory_t* m_factory = &model_factory);

    /**
     * @brief Initialize inference library.  
     * Initialize the library and start the background threads used for
     * model managment and sending actions and outcomes to the online trainer
     * @param status  Optional field with detailed string description if there is an error 
     * @return int Return error code.  This will also be returned in the api_status object
     */
    int init(api_status* status=nullptr);

    /**
     * @brief Choose an action, given a list of action, action features and context features. 
     * Choose an action by using the inferencing model to create a probability distribution over actions 
     * and then drawing from the distribution.
     * @param uuid  The unique identifier for this interaction.  The same uuid should be used when
     *              reporting the outcome for this action.  
     * @param context_json Contains action, action features and context features in json format
     * @param resp Ranking response contains the choosen action, probability distrubtion used for sampling actions and ranked actions
     * @param status  Optional field with detailed string description if there is an error 
     * @return int Return error code.  This will also be returned in the api_status object
     */
		int choose_rank(const char * uuid, const char * context_json, ranking_response& resp, api_status* status= nullptr);

    /**
     * @brief Choose an action, given a list of action, action features and context features. 
     * Choose an action by using the inferencing model to create a probability distribution over actions 
     * and then drawing from the distribution.  An unique id will be generated and returned in the ranking_response.
     * The same uuid should be used when reporting the outcome for this action.
     * 
     * @param context_json Contains action, action features and context features in json format
     * @param resp Ranking response contains the choosen action, probability distrubtion used for sampling actions and ranked actions
     * @param status  Optional field with detailed string description if there is an error 
     * @return int Return error code.  This will also be returned in the api_status object
     */
		int choose_rank(const char * context_json, ranking_response& resp, api_status* status= nullptr);//uuid is auto-generated

    /**
     * @brief Report the reward for the top action.  
     * 
     * @param uuid  The unique identifier used when choosing an action should be presented here.  This is so that
     *              the action taken can be matched with feeback recieved. 
     * @param reward Outcome/Reward serialized as a string
     * @param status  Optional field with detailed string description if there is an error 
     * @return int Return error code.  This will also be returned in the api_status object
     */
		int report_outcome(const char* uuid, const char* reward, api_status* status= nullptr);
		
    /**
     * @brief Report the reward for the top action.  
     * 
     * @param uuid  The unique identifier used when choosing an action should be presented here.  This is so that
     *              the action taken can be matched with feeback recieved. 
     * @param reward Outcome/Reward as float
     * @param status  Optional field with detailed string description if there is an error 
     * @return int Return error code.  This will also be returned in the api_status object
     */
    int report_outcome(const char* uuid, float reward, api_status* status= nullptr);

    /**
     * @brief Error callback function.  
     * When live_model is constructed, a background error callback and a 
     * context (void*) is registered. If there is an error in the background thread, 
     * error callback will get invoked with api_status and the context (void*).
     * This error callback is typed by the context used in the callback.
     * 
     * NOTE: Error callback will get invoked in a background thread. 
     * @tparam ErrCntxt Context type used when the error callback is invoked
     */
    template<typename ErrCntxt>
    using error_fn_t = void(*)( const api_status&, ErrCntxt* );

    /**
     * @brief Construct a new live model object.  
     * 
     * @tparam ErrCntxt Context type used in error callback.
     * @param config Name-Value based configuration
     * @param fn Error callback for handling errors in background thread
     * @param err_context Context passed back during Error callback
     * @param t_factory Transport factory.  The default transport factory is initialised with a 
     *                  REST based transport that gets data  from an Azure storage account
     * @param m_factory Model factory.  The default model factory hydrates vw models 
     *                    used for local inference.
     */
    template<typename ErrCntxt>
    explicit live_model(
      const utility::config_collection& config,
      error_fn_t<ErrCntxt> fn = nullptr,
      ErrCntxt* err_context = nullptr,
      transport_factory_t* t_factory = &data_transport_factory,
      model_factory_t* m_factory = &model_factory);

    /**
     * @brief Default move constructor for live model object.  
     */
    live_model(live_model&&) = default;

    /**
     * @brief Default move assignment operator swaps implementation.  
     */
    live_model& operator=(live_model&&) = default;

	  live_model(const live_model&) = delete;       //! Prevent accidental copy, since destructor will deallocate the implementation
    live_model& operator=(live_model&) = delete;  //! Prevent accidental copy, since destructor will deallocate the implementation

    ~live_model();

  private:
		live_model_impl* _pimpl;  //! The actual implementation details are forwarded to this object (PIMPL pattern)
    bool _initialized;        //! Guard to ensure that live_model is properly initialized. i.e. init() was called and successfully initialized.
	};

  /**
     * @brief Construct a new live model object.  
     * 
     * @tparam ErrCntxt Context type used in error callback.
     * @param config Name-Value based configuration
     * @param fn Error callback for handling errors in background thread
     * @param err_context Context passed back during Error callback
     * @param t_factory Transport factory.  The default transport factory is initialised with a 
     *                  REST based transport that gets data  from an Azure storage account
     * @param m_factory Model factory.  The default model factory hydrates vw models 
     *                    used for local inference.
   */
  template <typename ErrCtxt>
  live_model::live_model(
    const utility::config_collection& config,
    error_fn_t<ErrCtxt> fn,
    ErrCtxt* err_context,
    transport_factory_t* t_factory,
    model_factory_t* m_factory) 
  : live_model(config, (error_fn)(fn), (void*)(err_context), t_factory, m_factory) { }
}
