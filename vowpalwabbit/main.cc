/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD
license as described in the file LICENSE.
 */
#ifdef _WIN32
#include <WinSock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#include <sys/timeb.h>
#include "parse_args.h"
#include "parse_regressor.h"
#include "accumulate.h"
#include "best_constant.h"
#include "vw_exception.h"
#include <fstream>

#include "options.h"
#include "options_boost_po.h"

using namespace std;
using namespace VW::config;

vw* setup(options_i& options)
{
  vw* all = nullptr;
  try
  {
    all = VW::initialize(options);
  }
  catch (const exception& ex)
  {
    cout << ex.what() << endl;
    throw;
  }
  catch (...)
  {
    cout << "unknown exception" << endl;
    throw;
  }
  all->vw_is_main = true;

  if (!all->quiet && !all->bfgs && !all->searchstr && !options.was_supplied("audit_regressor"))
  {
    all->trace_message << std::left << std::setw(shared_data::col_avg_loss) << std::left << "average"
                       << " " << std::setw(shared_data::col_since_last) << std::left << "since"
                       << " " << std::right << std::setw(shared_data::col_example_counter) << "example"
                       << " " << std::setw(shared_data::col_example_weight) << "example"
                       << " " << std::setw(shared_data::col_current_label) << "current"
                       << " " << std::setw(shared_data::col_current_predict) << "current"
                       << " " << std::setw(shared_data::col_current_features) << "current" << std::endl;
    all->trace_message << std::left << std::setw(shared_data::col_avg_loss) << std::left << "loss"
                       << " " << std::setw(shared_data::col_since_last) << std::left << "last"
                       << " " << std::right << std::setw(shared_data::col_example_counter) << "counter"
                       << " " << std::setw(shared_data::col_example_weight) << "weight"
                       << " " << std::setw(shared_data::col_current_label) << "label"
                       << " " << std::setw(shared_data::col_current_predict) << "predict"
                       << " " << std::setw(shared_data::col_current_features) << "features" << std::endl;
  }

  return all;
}

int main(int argc, char* argv[])
{
  bool should_use_onethread = false;
  option_group_definition driver_config("driver");
  driver_config.add(make_option("onethread", should_use_onethread).help("Disable parse thread"));

  try
  {
    // support multiple vw instances for training of the same datafile for the same instance
    vector<std::unique_ptr<options_boost_po>> arguments;
    vector<vw*> alls;
    if (argc == 3 && !strcmp(argv[1], "--args"))
    {
      std::fstream arg_file(argv[2]);

      int line_count = 1;
      std::string line;
      while (std::getline(arg_file, line))
      {
        std::stringstream sstr;
        sstr << line << " -f model." << (line_count++);
        sstr << " --no_stdin";  // can't use stdin with multiple models

        std::cout << sstr.str() << endl;
        string str = sstr.str();
        const char* new_args = str.c_str();

        int l_argc;
        char** l_argv = VW::get_argv_from_string(new_args, l_argc);

        std::unique_ptr<options_boost_po> ptr(new options_boost_po(argc, argv));
        ptr->add_and_parse(driver_config);
        alls.push_back(setup(*ptr.get()));
        arguments.push_back(std::move(ptr));
      }
    }
    else
    {
      std::unique_ptr<options_boost_po> ptr(new options_boost_po(argc, argv));
      ptr->add_and_parse(driver_config);
      alls.push_back(setup(*ptr.get()));
      arguments.push_back(std::move(ptr));
    }

    vw& all = *alls[0];

    // struct timeb t_start, t_end;
    // ftime(&t_start);

    if (should_use_onethread)
    {
      if (alls.size() == 1)
        LEARNER::generic_driver_onethread(all);
      else
        throw "--onethread doesn't make sense with multiple learners";
    }
    else
    {
      VW::start_parser(all);
      if (alls.size() == 1)
        LEARNER::generic_driver(all);
      else
        LEARNER::generic_driver(alls);
      VW::end_parser(all);
    }

    for (vw* v : alls)
    {
      if (v->p->exc_ptr)
      {
        std::rethrow_exception(v->p->exc_ptr);
      }

      VW::sync_stats(*v);
      VW::finish(*v);
    }
  }
  catch (VW::vw_exception& e)
  {
    cerr << "vw (" << e.Filename() << ":" << e.LineNumber() << "): " << e.what() << endl;
    exit(1);
  }
  catch (exception& e)
  {
    // vw is implemented as a library, so we use 'throw runtime_error()'
    // error 'handling' everywhere.  To reduce stderr pollution
    // everything gets caught here & the error message is printed
    // sans the excess exception noise, and core dump.
    cerr << "vw: " << e.what() << endl;
    // cin.ignore();
    exit(1);
  }
  // cin.ignore();
  return 0;
}
