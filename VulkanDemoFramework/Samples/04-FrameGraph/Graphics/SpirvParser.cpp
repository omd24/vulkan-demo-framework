#include "SpirvParser.hpp"

#include "Foundation/Numerics.hpp"
#include "Foundation/String.hpp"

#include <string.h>

namespace Graphics
{
//---------------------------------------------------------------------------//
namespace Spirv
{
static const uint32_t kBindlessTextureBinding = 10;
//---------------------------------------------------------------------------//
struct Member
{
  uint32_t idIndex;
  uint32_t offset;

  Framework::StringView name;
};
//---------------------------------------------------------------------------//
struct Id
{
  SpvOp op;
  uint32_t set;
  uint32_t binding;

  // For integers and floats
  uint8_t width;
  uint8_t sign;

  // For arrays, vectors and matrices
  uint32_t typeIndex;
  uint32_t count;

  // For variables
  SpvStorageClass storageClass;

  // For constants
  uint32_t value;

  // For structs
  Framework::StringView name;
  Framework::Array<Member> members;
};
//---------------------------------------------------------------------------//
VkShaderStageFlags parseExecutionModel(SpvExecutionModel model)
{
  switch (model)
  {
  case (SpvExecutionModelVertex): {
    return VK_SHADER_STAGE_VERTEX_BIT;
  }
  case (SpvExecutionModelGeometry): {
    return VK_SHADER_STAGE_GEOMETRY_BIT;
  }
  case (SpvExecutionModelFragment): {
    return VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  case (SpvExecutionModelKernel): {
    return VK_SHADER_STAGE_COMPUTE_BIT;
  }
  }

  return 0;
}
//---------------------------------------------------------------------------//
void parseBinary(
    const uint32_t* p_Data,
    size_t p_DataSize,
    Framework::StringBuffer& p_NameBuffer,
    ParseResult* p_ParseResult)
{
  assert((p_DataSize % 4) == 0);
  uint32_t spvWordCount = static_cast<uint32_t>(p_DataSize / 4);

  uint32_t magicNumber = p_Data[0];
  assert(magicNumber == 0x07230203);

  uint32_t idBound = p_Data[3];

  Framework::Allocator* allocator = &Framework::MemoryService::instance()->m_SystemAllocator;
  Framework::Array<Id> ids;
  ids.init(allocator, idBound, idBound);

  memset(ids.m_Data, 0, idBound * sizeof(Id));

  VkShaderStageFlags stage;

  size_t wordIndex = 5;
  while (wordIndex < spvWordCount)
  {
    SpvOp op = (SpvOp)(p_Data[wordIndex] & 0xFF);
    uint16_t wordCount = (uint16_t)(p_Data[wordIndex] >> 16);

    switch (op)
    {

    case (SpvOpEntryPoint): {
      assert(wordCount >= 4);

      SpvExecutionModel model = (SpvExecutionModel)p_Data[wordIndex + 1];

      stage = parseExecutionModel(model);
      assert(stage != 0);

      break;
    }

    case (SpvOpDecorate): {
      assert(wordCount >= 3);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];

      SpvDecoration decoration = (SpvDecoration)p_Data[wordIndex + 2];
      switch (decoration)
      {
      case (SpvDecorationBinding): {
        id.binding = p_Data[wordIndex + 3];
        break;
      }

      case (SpvDecorationDescriptorSet): {
        id.set = p_Data[wordIndex + 3];
        break;
      }
      }

      break;
    }

    case (SpvOpMemberDecorate): {
      assert(wordCount >= 4);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];

      uint32_t memberIndex = p_Data[wordIndex + 2];

      if (id.members.m_Capacity == 0)
      {
        id.members.init(allocator, 64, 64);
      }

      Member& member = id.members[memberIndex];

      SpvDecoration decoration = (SpvDecoration)p_Data[wordIndex + 3];
      switch (decoration)
      {
      case (SpvDecorationOffset): {
        member.offset = p_Data[wordIndex + 4];
        break;
      }
      }

      break;
    }

    case (SpvOpName): {
      assert(wordCount >= 3);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];

      char* name = (char*)(p_Data + (wordIndex + 2));
      char* nameView = p_NameBuffer.appendUse(name);

      id.name.m_Text = nameView;
      id.name.m_Length = strlen(nameView);

      break;
    }

    case (SpvOpMemberName): {
      assert(wordCount >= 4);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];

      uint32_t memberIndex = p_Data[wordIndex + 2];

      if (id.members.m_Capacity == 0)
      {
        id.members.init(allocator, 64, 64);
      }

      Member& member = id.members[memberIndex];

      char* name = (char*)(p_Data + (wordIndex + 3));
      char* nameView = p_NameBuffer.appendUse(name);

      member.name.m_Text = nameView;
      member.name.m_Length = strlen(nameView);

      break;
    }

    case (SpvOpTypeInt): {
      assert(wordCount == 4);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];
      id.op = op;
      id.width = (uint8_t)p_Data[wordIndex + 2];
      id.sign = (uint8_t)p_Data[wordIndex + 3];

      break;
    }

    case (SpvOpTypeFloat): {
      assert(wordCount == 3);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];
      id.op = op;
      id.width = (uint8_t)p_Data[wordIndex + 2];

      break;
    }

    case (SpvOpTypeVector): {
      assert(wordCount == 4);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];
      id.op = op;
      id.typeIndex = p_Data[wordIndex + 2];
      id.count = p_Data[wordIndex + 3];

      break;
    }

    case (SpvOpTypeMatrix): {
      assert(wordCount == 4);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];
      id.op = op;
      id.typeIndex = p_Data[wordIndex + 2];
      id.count = p_Data[wordIndex + 3];

      break;
    }

    case (SpvOpTypeImage): {
      // NOTE: not sure we need this information just yet
      assert(wordCount >= 9);

      break;
    }

    case (SpvOpTypeSampler): {
      assert(wordCount == 2);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];
      id.op = op;

      break;
    }

    case (SpvOpTypeSampledImage): {
      assert(wordCount == 3);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];
      id.op = op;

      break;
    }

    case (SpvOpTypeArray): {
      assert(wordCount == 4);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];
      id.op = op;
      id.typeIndex = p_Data[wordIndex + 2];
      id.count = p_Data[wordIndex + 3];

      break;
    }

    case (SpvOpTypeRuntimeArray): {
      assert(wordCount == 3);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];
      id.op = op;
      id.typeIndex = p_Data[wordIndex + 2];

      break;
    }

    case (SpvOpTypeStruct): {
      assert(wordCount >= 2);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];
      id.op = op;

      if (wordCount > 2)
      {
        for (uint16_t memberIndex = 0; memberIndex < wordCount - 2; ++memberIndex)
        {
          id.members[memberIndex].idIndex = p_Data[wordIndex + memberIndex + 2];
        }
      }

      break;
    }

    case (SpvOpTypePointer): {
      assert(wordCount == 4);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];
      id.op = op;
      id.typeIndex = p_Data[wordIndex + 3];

      break;
    }

    case (SpvOpConstant): {
      assert(wordCount >= 4);

      uint32_t idIndex = p_Data[wordIndex + 1];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];
      id.op = op;
      id.typeIndex = p_Data[wordIndex + 2];
      id.value = p_Data[wordIndex + 3]; // NOTE: we assume all constants to have maximum
                                        // 32bit width

      break;
    }

    case (SpvOpVariable): {
      assert(wordCount >= 4);

      uint32_t idIndex = p_Data[wordIndex + 2];
      assert(idIndex < idBound);

      Id& id = ids[idIndex];
      id.op = op;
      id.typeIndex = p_Data[wordIndex + 1];
      id.storageClass = (SpvStorageClass)p_Data[wordIndex + 3];

      break;
    }
    }

    wordIndex += wordCount;
  }

  for (uint32_t idIndex = 0; idIndex < ids.m_Size; ++idIndex)
  {
    Id& id = ids[idIndex];

    if (id.op == SpvOpVariable)
    {
      switch (id.storageClass)
      {
      case (SpvStorageClassUniform):
      case (SpvStorageClassUniformConstant): {
        if (id.set == 1 &&
            (id.binding == kBindlessTextureBinding || id.binding == (kBindlessTextureBinding + 1)))
        {
          // NOTE: these are managed by the GPU device
          continue;
        }

        // NOTE: get actual type
        Id& uniform_type = ids[ids[id.typeIndex].typeIndex];

        DescriptorSetLayoutCreation& setLayout = p_ParseResult->sets[id.set];
        setLayout.setSetIndex(id.set);

        DescriptorSetLayoutCreation::Binding binding{};
        binding.index = id.binding;
        binding.count = 1;

        switch (uniform_type.op)
        {
        case (SpvOpTypeStruct): {
          binding.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
          binding.name = uniform_type.name.m_Text;
          break;
        }

        case (SpvOpTypeSampledImage): {
          binding.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
          binding.name = id.name.m_Text;
          break;
        }
        }

        setLayout.addBindingAtIndex(binding, id.binding);

        p_ParseResult->setCount = Framework::max(p_ParseResult->setCount, (id.set + 1));

        break;
      }
      }
    }

    id.members.shutdown();
  }

  ids.shutdown();
}
//---------------------------------------------------------------------------//
} // namespace Spirv
//---------------------------------------------------------------------------//
} // namespace Graphics
