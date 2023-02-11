#include <Foundation/File.hpp>
#include <Foundation/Gltf.hpp>
#include <Foundation/Numerics.hpp>
#include <Foundation/ResourceManager.hpp>
#include <Foundation/Time.hpp>

#include <Application/Window.hpp>

#include <stdlib.h> // for exit()

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    printf("No model specified, using the default model\n");
    exit(-1);
  }
  return (0);
}
