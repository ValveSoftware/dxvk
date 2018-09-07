#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>

#include "../util/thread.h"
#include "dxvk_context.h"

namespace dxvk {
  
  /**
   * \brief Command stream operation
   * 
   * An abstract representation of an operation
   * that can be recorded into a command list.
   */
  class DxvkCsCmd {
    
  public:
    
    virtual ~DxvkCsCmd() { }
    
    /**
     * \brief Retrieves next command in a command chain
     * 
     * This can be used to quickly iterate
     * over commands within a chunk.
     * \returns Pointer the next command
     */
    DxvkCsCmd* next() const {
      return m_next;
    }
    
    /**
     * \brief Sets next command in a command chain
     * \param [in] next Next command
     */
    void setNext(DxvkCsCmd* next) {
      m_next = next;
    }
    
    /**
     * \brief Executes embedded commands
     * \param [in] ctx The target context
     */
    virtual void exec(DxvkContext* ctx) const = 0;
    
  private:
    
    DxvkCsCmd* m_next = nullptr;
    
  };
  
  
  /**
   * \brief Typed command
   * 
   * Stores a function object which is
   * used to execute an embedded command.
   */
  template<typename T>
  class alignas(16) DxvkCsTypedCmd : public DxvkCsCmd {
    
  public:
    
    DxvkCsTypedCmd(T&& cmd)
    : m_command(std::move(cmd)) { }
    
    DxvkCsTypedCmd             (DxvkCsTypedCmd&&) = delete;
    DxvkCsTypedCmd& operator = (DxvkCsTypedCmd&&) = delete;
    
    void exec(DxvkContext* ctx) const {
      m_command(ctx);
    }
    
  private:
    
    T m_command;
    
  };
  
  
  /**
   * \brief Command chunk
   * 
   * Stores a list of commands.
   */
  class DxvkCsChunk : public RcObject {
    constexpr static size_t MaxBlockSize = 16384;
  public:
    
    DxvkCsChunk();
    ~DxvkCsChunk();
    
    /**
     * \brief Number of commands recorded to the chunk
     * 
     * Can be used to check whether the chunk needs to
     * be dispatched or just to keep track of statistics.
     */
    size_t commandCount() const {
      return m_commandCount;
    }
    
    /**
     * \brief Tries to add a command to the chunk
     * 
     * If the given command can be added to the chunk, it
     * will be consumed. Otherwise, a new chunk must be
     * created which is large enough to hold the command.
     * \param [in] command The command to add
     * \returns \c true on success, \c false if
     *          a new chunk needs to be allocated
     */
    template<typename T>
    bool push(T& command) {
      using FuncType = DxvkCsTypedCmd<T>;
      
      if (m_commandOffset + sizeof(FuncType) > MaxBlockSize)
        return false;
      
      DxvkCsCmd* tail = m_tail;
      
      m_tail = new (m_data + m_commandOffset)
        FuncType(std::move(command));
      
      if (tail != nullptr)
        tail->setNext(m_tail);
      else
        m_head = m_tail;
      
      m_commandCount  += 1;
      m_commandOffset += sizeof(FuncType);
      return true;
    }
    
    /**
     * \brief Executes all commands
     * 
     * This will also reset the chunk
     * so that it can be reused.
     * \param [in] ctx The context
     */
    void executeAll(DxvkContext* ctx);
    
    /**
     * \brief Resets chunk
     * 
     * Destroys all recorded commands and
     * marks the chunk itself as empty, so
     * that it can be reused later.
     */
    void reset();
    
  private:
    
    size_t m_commandCount  = 0;
    size_t m_commandOffset = 0;
    
    DxvkCsCmd* m_head = nullptr;
    DxvkCsCmd* m_tail = nullptr;
    
    alignas(64)
    char m_data[MaxBlockSize];
    
  };
  
  
  /**
   * \brief Chunk pool
   * 
   * Implements a pool of CS chunks which can be
   * recycled. The goal is to reduce the number
   * of dynamic memory allocations.
   */
  class DxvkCsChunkPool {
    
  public:
    
    DxvkCsChunkPool();
    ~DxvkCsChunkPool();
    
    DxvkCsChunkPool             (const DxvkCsChunkPool&) = delete;
    DxvkCsChunkPool& operator = (const DxvkCsChunkPool&) = delete;
    
    /**
     * \brief Allocates a chunk
     * 
     * Takes an existing chunk from the pool,
     * or creates a new one if necessary.
     * \returns Allocated chunk object
     */
    DxvkCsChunk* allocChunk();
    
    /**
     * \brief Releases a chunk
     * 
     * Resets the chunk and adds it to the pool.
     * \param [in] chunk Chunk to release
     */
    void freeChunk(DxvkCsChunk* chunk);
    
  private:
    
    sync::Spinlock            m_mutex;
    std::vector<DxvkCsChunk*> m_chunks;
    
  };
  
  
  /**
   * \brief Chunk reference
   * 
   * Implements basic reference counting for
   * CS chunks and returns them to the pool
   * as soon as they are no longer needed.
   */
  class DxvkCsChunkRef {
    
  public:
    
    DxvkCsChunkRef() { }
    DxvkCsChunkRef(
      DxvkCsChunk*      chunk,
      DxvkCsChunkPool*  pool)
    : m_chunk (chunk),
      m_pool  (pool) {
      this->incRef();
    }
    
    DxvkCsChunkRef(const DxvkCsChunkRef& other)
    : m_chunk (other.m_chunk),
      m_pool  (other.m_pool) {
      this->incRef();
    }
    
    DxvkCsChunkRef(DxvkCsChunkRef&& other)
    : m_chunk (other.m_chunk),
      m_pool  (other.m_pool) {
      other.m_chunk = nullptr;
      other.m_pool  = nullptr;
    }
    
    DxvkCsChunkRef& operator = (const DxvkCsChunkRef& other) {
      other.incRef();
      this->decRef();
      this->m_chunk = other.m_chunk;
      this->m_pool  = other.m_pool;
      return *this;
    }
    
    DxvkCsChunkRef& operator = (DxvkCsChunkRef&& other) {
      this->decRef();
      this->m_chunk = other.m_chunk;
      this->m_pool  = other.m_pool;
      other.m_chunk = nullptr;
      other.m_pool  = nullptr;
      return *this;
    }
    
    ~DxvkCsChunkRef() {
      this->decRef();
    }
    
    DxvkCsChunk* operator -> () const {
      return m_chunk;
    }
    
    operator bool () const {
      return m_chunk != nullptr;
    }
    
  private:
    
    DxvkCsChunk*      m_chunk = nullptr;
    DxvkCsChunkPool*  m_pool  = nullptr;
    
    void incRef() const {
      if (m_chunk != nullptr)
        m_chunk->incRef();
    }
    
    void decRef() const {
      if (m_chunk != nullptr && m_chunk->decRef() == 0)
        m_pool->freeChunk(m_chunk);
    }
    
  };
  
  
  /**
   * \brief Command stream thread
   * 
   * Spawns a thread that will execute
   * commands on a DXVK context. 
   */
  class DxvkCsThread {
    
  public:
    
    DxvkCsThread(const Rc<DxvkContext>& context);
    ~DxvkCsThread();
    
    /**
     * \brief Dispatches an entire chunk
     * 
     * Can be used to efficiently play back large
     * command lists recorded on another thread.
     * \param [in] chunk The chunk to dispatch
     */
    void dispatchChunk(DxvkCsChunkRef&& chunk);
    
    /**
     * \brief Synchronizes with the thread
     * 
     * This waits for all chunks in the dispatch
     * queue to be processed by the thread. Note
     * that this does \e not implicitly call
     * \ref flush.
     */
    void synchronize();
    
  private:
    
    const Rc<DxvkContext>       m_context;
    
    std::atomic<bool>           m_stopped = { false };
    std::mutex                  m_mutex;
    std::condition_variable     m_condOnAdd;
    std::condition_variable     m_condOnSync;
    std::queue<DxvkCsChunkRef>  m_chunksQueued;
    dxvk::thread                m_thread;
    
    uint32_t                    m_chunksPending = 0;
    
    void threadFunc();
    
  };
  
}
