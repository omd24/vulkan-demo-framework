#include "CommandBuffer.hpp"

namespace Graphics
{
void CommandBuffer::init(QueueType::Enum p_Type, uint32_t p_BufferSize, uint32_t p_SubmitSize)
{
  this->m_Type = p_Type;
  this->m_BufferSize = p_BufferSize;

  reset();
}
void CommandBuffer::terminate() { m_IsRecording = false; }
void CommandBuffer::reset()
{
  m_IsRecording = false;
  m_CurrentRenderPass = nullptr;
  m_CurrentPipeline = nullptr;
  m_CurrentCommand = 0;
}
} // namespace Graphics
