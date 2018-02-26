// Distributed under the MIT License.
// See LICENSE.txt for details.

/// \file
/// Defines functions used by Charm++ to register classes/chares and entry
/// methods

#include <algorithm>
#include <string>
#include <type_traits>
#include <vector>

#include "Parallel/CharmRegistration.hpp"
#include "Parallel/ConstGlobalCache.hpp"
#include "Parallel/Main.hpp"
#include "Utilities/NoSuchType.hpp"

namespace Parallel {
namespace charmxx {
/// \cond
std::unique_ptr<RegistrationHelper>* charm_register_list = nullptr;
size_t charm_register_list_capacity = 0;
size_t charm_register_list_size = 0;
/// \endcond

template <class...>
using void_t = void;

template <class T>
struct get_value_type;

template <class Key, class Mapped, class Hash, class KeyEqual, class Allocator>
struct get_value_type<
    std::unordered_map<Key, Mapped, Hash, KeyEqual, Allocator>> {
  // When sending data it is typical to use `std:make_pair(a, b)` which results
  // in a non-const Key type, which is different from what
  // `unordered_map::value_type` is. This difference leads to issues with
  // function registration with Charm++.
  using type = std::pair<Key, Mapped>;
};

template <class Key, class Hash, class KeyEqual, class Allocator>
struct get_value_type<std::unordered_multiset<Key, Hash, KeyEqual, Allocator>> {
  using type = Key;
};

template <class T>
using get_value_type_t = typename get_value_type<T>::type;

template <class T, class = void>
struct has_single_actions : std::false_type {};
template <class T>
struct has_single_actions<T, void_t<typename T::explicit_single_actions_list>>
    : std::true_type {};

template <class T, class = void>
struct has_reduction_actions : std::false_type {};
template <class T>
struct has_reduction_actions<T, void_t<typename T::reduction_actions_list>>
    : std::true_type {};

template <class... Ts>
void swallow(Ts&&... /*ts*/) {}

template <class ParallelComponent>
struct CharmRegisterFunctions {
  using chare_type = typename ParallelComponent::chare_type;
  using charm_type = charm_types_with_parameters<
      ParallelComponent, typename ParallelComponent::metavariables,
      typename ParallelComponent::action_list,
      typename get_array_index<chare_type>::template f<ParallelComponent>,
      typename ParallelComponent::initial_databox>;
  using cproxy = typename charm_type::cproxy;
  using ckindex = typename charm_type::ckindex;
  using algorithm = typename charm_type::algorithm;

  static void register_algorithm() {
    ckindex::__register(get_template_parameters_as_string<algorithm>().c_str(),
                        sizeof(algorithm));
  }

  template <class... ReceiveTags,
            typename std::enable_if<
                (sizeof...(ReceiveTags),
                 not Parallel::is_group_proxy<cproxy>::value and
                     not Parallel::is_node_group_proxy<cproxy>::value)>::type* =
                nullptr>
  static void register_receive_data(tmpl::list<ReceiveTags...> /*meta*/) {
    swallow(((void)ckindex::template idx_receive_data<ReceiveTags>(
                 static_cast<void (algorithm::*)(
                     const typename ReceiveTags::temporal_id&,
                     const get_value_type_t<
                         typename ReceiveTags::type::mapped_type>&,
                     bool)>(nullptr)),
             0)...);
  }

  template <class... ReceiveTags,
            typename std::enable_if<(sizeof...(ReceiveTags),
                                     Parallel::is_group_proxy<cproxy>::value or
                                         Parallel::is_node_group_proxy<
                                             cproxy>::value)>::type* = nullptr>
  static void register_receive_data(tmpl::list<ReceiveTags...> /*meta*/) {
    swallow((
        (void)ckindex::template idx_receive_data<ReceiveTags>(
            static_cast<void (algorithm::*)(
                const typename ReceiveTags::temporal_id&,
                const typename std::decay<typename ReceiveTags::type::
                                              mapped_type::value_type>::type&)>(
                nullptr)),
        0)...);
  }

  template <class... Actions>
  static void register_reduction_actions(tmpl::list<Actions...> /*meta*/) {
    swallow(((void)ckindex::template idx_reduction_action<
                 Actions, typename Actions::reduction_type>(
                 static_cast<void (algorithm::*)(
                     const typename Actions::reduction_type&)>(nullptr)),
             (void)ckindex::template redn_wrapper_reduction_action<
                 Actions, typename Actions::reduction_type>(nullptr),
             0)...);
  }
};

template <class ParallelComponent>
void register_reduction_actions(std::true_type /*meta*/) {
  CharmRegisterFunctions<ParallelComponent>::register_reduction_actions(
      typename ParallelComponent::reduction_actions_list{});
}

template <class ParallelComponent>
void register_reduction_actions(std::false_type /*meta*/) {}

template <class... ParallelComponents>
void register_parallel_components(tmpl::list<ParallelComponents...> /*meta*/) {
  static bool done_registration{false};
  if (done_registration) {
    return;  // LCOV_EXCL_LINE
  }
  done_registration = true;

  // Charm++ requires the order of registration to be the same across all
  // processors. To make sure this is satisfied regardless of any possible weird
  // behavior with static variable initialization we sort the list before
  // registering anything.
  // clang-tidy: do not user pointer arithmetic
  std::sort(charm_register_list,
            charm_register_list + charm_register_list_size,  // NOLINT
            [](const std::unique_ptr<RegistrationHelper>& a,
               const std::unique_ptr<RegistrationHelper>& b) {
              return a->name() < b->name();
            });

  swallow(
      ((void)CharmRegisterFunctions<ParallelComponents>::register_algorithm(),
       0)...);
  swallow((
      (void)CharmRegisterFunctions<ParallelComponents>::register_receive_data(
          Parallel::get_inbox_tags<typename ParallelComponents::action_list>{}),
      0)...);
  // register simple actions
  for (size_t i = 0;
       i < charm_register_list_size and i < charm_register_list_capacity; ++i) {
    // clang-tidy: do not use pointer arithmetic
    charm_register_list[i]->register_with_charm();  // NOLINT
  }
  delete[] charm_register_list;
  swallow(((void)register_reduction_actions<ParallelComponents>(
               typename has_reduction_actions<ParallelComponents>::type{}),
           0)...);
}

void register_init_node_and_proc() {
  static bool done_registration{false};
  if (done_registration) {
    return;  // LCOV_EXCL_LINE
  }
  done_registration = true;
  for (auto& init_node_func : charm_init_node_funcs) {
    _registerInitCall(*init_node_func, 1);
  }

  for (auto& init_proc_func : charm_init_proc_funcs) {
    _registerInitCall(*init_proc_func, 0);
  }
}

template <class Metavariables>
void register_main_and_cache() {
  static bool done_registration{false};
  if (done_registration) {
    return;  // LCOV_EXCL_LINE
  }
  done_registration = true;
  // Get the metavariables name, strip anonymous namespace references,
  // then register Main and ConstGlobalCache
  std::string metavariables_name =
      Parallel::charmxx::get_template_parameters_as_string<Metavariables>();
  if (metavariables_name.find(std::string("(anonymousnamespace)::")) !=
      std::string::npos) {
    metavariables_name =
        metavariables_name.substr(std::string("(anonymousnamespace)::").size(),
                                  metavariables_name.size());
  }
  Parallel::CkIndex_Main<Metavariables>::__register(
      std::string("Main<" + metavariables_name + std::string(" >")).c_str(),
      sizeof(Parallel::Main<Metavariables>));
  Parallel::CkIndex_ConstGlobalCache<Metavariables>::__register(
      std::string("ConstGlobalCache<" + metavariables_name + std::string(" >"))
          .c_str(),
      sizeof(Parallel::ConstGlobalCache<Metavariables>));
}

}  // namespace charmxx
}  // namespace Parallel

extern "C" void CkRegisterMainModule() {
  Parallel::charmxx::register_init_node_and_proc();
  Parallel::charmxx::register_main_and_cache<charm_metavariables>();
  Parallel::charmxx::register_parallel_components(
      typename charm_metavariables::component_list{});
}
