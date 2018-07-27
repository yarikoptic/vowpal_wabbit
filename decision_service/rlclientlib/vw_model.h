#pragma once
#include "model_mgmt.h"

namespace reinforcement_learning { namespace model_management {
  class vw_model : public i_model{
  public:
    int init(model_data& data, api_status* status = nullptr) override;
    int choose_rank(char* features, int actions[], api_status* status = nullptr) override;
  };
}}
