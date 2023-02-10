#include "App.hpp"

namespace Framework
{
void App::run(const ApplicationConfiguration& p_Configuration)
{
  create(p_Configuration);
  mainLoop();
  destroy();
}
} // namespace Framework
