#pragma once

#include <core/base/cancellation.hpp>
#include <core/base/error.hpp>
#include <core/base/progress.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace b_launcher_base::install
{

class task_graph
{
public:
  using task_fn = std::function<result<void> (cancellation_token)>;

  std::size_t add (std::string name, task_fn fn,
                   std::vector<std::size_t> dependencies = {});
  result<void> run (cancellation_token token = {},
                    progress_sink *sink = nullptr);
  std::size_t size () const noexcept;
  const std::string &name (std::size_t id) const;

private:
  struct node
  {
    std::string name;
    task_fn fn;
    std::vector<std::size_t> dependencies;
  };

  std::vector<node> m_nodes;
};

}
