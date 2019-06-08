#include "options_boost_po.h"

#include <sstream>

#include <algorithm>
#include <iterator>

using namespace VW::config;

template <>
po::typed_value<std::vector<bool>>* options_boost_po::convert_to_boost_value(std::shared_ptr<typed_option<bool>>& opt)
{
  auto value = get_base_boost_value(opt);

  if (opt->default_value_supplied())
  {
    THROW("Using a bool option type acts as a switch, no explicit default value is allowed.")
  }

  value->default_value({false});
  value->zero_tokens();
  value->implicit_value({true});

  return add_notifier(opt, value);
}

void options_boost_po::add_to_description(
    std::shared_ptr<base_option> opt, po::options_description& options_description)
{
  add_to_description_impl<supported_options_types>(opt, options_description);
}

void options_boost_po::add_and_parse(const option_group_definition& group)
{
  po::options_description new_options(group.m_name);

  for (auto opt_ptr : group.m_options)
  {
    add_to_description(opt_ptr, new_options);
    m_defined_options.insert(opt_ptr->m_name);
    m_defined_options.insert(opt_ptr->m_short_name);
    m_defined_options.insert("-" + opt_ptr->m_short_name);

    // The last definition is kept. There was a bug where using .insert at a later pointer changed the command line but
    // the previously defined option's default value was serialized into the model. This resolves that state info.
    m_options[opt_ptr->m_name] = opt_ptr;
  }

  // Add the help for the given options.
  new_options.print(m_help_stringstream);

  try
  {
    po::variables_map vm;
    auto parsed_options = po::command_line_parser(m_command_line)
                              .options(new_options)
                              .style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing)
                              .allow_unregistered()
                              .run();

    for (auto const& option : parsed_options.options)
    {
      m_supplied_options.insert(option.string_key);

      // If a string is later determined to be a value the erase it. This happens for negative numbers "-2"
      for (auto& val : option.value)
      {
        m_ignore_supplied.insert(val);
      }

      // Parsed options can contain short options in the form -k, we can only check these as the group definitions come
      // in.
      if (option.string_key.length() > 0 && option.string_key[0] == '-')
      {
        auto short_name = option.string_key.substr(1);
        for (auto opt_ptr : group.m_options)
        {
          if (opt_ptr->m_short_name == short_name)
          {
            m_supplied_options.insert(short_name);
          }
        }
      }
    }

    po::store(parsed_options, vm);
    po::notify(vm);
  }
  catch (boost::exception_detail::clone_impl<
      boost::exception_detail::error_info_injector<boost::program_options::invalid_option_value>>& ex)
  {
    THROW_EX(VW::vw_argument_invalid_value_exception, ex.what());
  }
  catch (boost::exception_detail::clone_impl<boost::exception_detail::error_info_injector<boost::bad_lexical_cast>>& ex)
  {
    THROW_EX(VW::vw_argument_invalid_value_exception, ex.what());
  }
  catch (boost::exception_detail::clone_impl<
      boost::exception_detail::error_info_injector<boost::program_options::ambiguous_option>>& ex)
  {
    THROW(ex.what());
  }
  catch (boost::program_options::ambiguous_option& ex)
  {
    THROW(ex.what());
  }
}

bool options_boost_po::was_supplied(const std::string& key)
{
  // Best check, only valid after options parsed.
  if (m_supplied_options.count(key) > 0)
  {
    return true;
  }

  // Basic check, string match against command line.
  auto it = std::find(m_command_line.begin(), m_command_line.end(), std::string("--" + key));
  return it != m_command_line.end();
}

std::string options_boost_po::help() { return m_help_stringstream.str(); }

std::vector<std::shared_ptr<base_option>> options_boost_po::get_all_options()
{
  std::vector<std::shared_ptr<base_option>> output_values;

  std::transform(m_options.begin(), m_options.end(), std::back_inserter(output_values),
      [](std::pair<const std::string, std::shared_ptr<base_option>>& kv) { return kv.second; });

  return output_values;
}

std::shared_ptr<base_option> VW::config::options_boost_po::get_option(const std::string& key)
{
  auto it = m_options.find(key);
  if (it != m_options.end())
  {
    return it->second;
  }

  throw std::out_of_range(key + " was not found.");
}

// Check all supplied arguments against defined args.
void options_boost_po::check_unregistered()
{
  for (auto const& supplied : m_supplied_options)
  {
    if (m_defined_options.count(supplied) == 0 && m_ignore_supplied.count(supplied) == 0)
    {
      THROW_EX(VW::vw_unrecognised_option_exception, "unrecognised option '--" << supplied << "'");
    }
  }
}

template <>
void options_boost_po::add_to_description_impl<typelist<>>(std::shared_ptr<base_option>, po::options_description&)
{
  THROW("That is an unsupported option type.");
}
