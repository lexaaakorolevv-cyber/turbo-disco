#include "save_worker.hpp"
namespace rf {
SearchSaveWorker::SearchSaveWorker():m_thread([this]{run();}){}
SearchSaveWorker::~SearchSaveWorker(){drain();{std::lock_guard lock(m_mutex);m_stopping=true;}m_ready.notify_one();m_thread.join();}
void SearchSaveWorker::submit(SearchSaveJob job){
 std::unique_lock lock(m_mutex);
 if(m_pending&&m_pending->sessionPath!=job.sessionPath){lock.unlock();drain();lock.lock();}
 if(m_pending)job.writeBest=job.writeBest||m_pending->writeBest;
 m_pending=std::move(job);lock.unlock();m_ready.notify_one();
}
void SearchSaveWorker::drain(){std::unique_lock lock(m_mutex);m_idle.wait(lock,[&]{return !m_busy&&!m_pending;});}
std::optional<SearchSaveResult> SearchSaveWorker::result(){std::lock_guard lock(m_mutex);auto r=std::move(m_result);m_result.reset();return r;}
void SearchSaveWorker::run(){
 for(;;){
  std::unique_lock lock(m_mutex);m_ready.wait(lock,[&]{return m_stopping||m_pending.has_value();});if(m_stopping)return;
  auto job=std::move(*m_pending);m_pending.reset();m_busy=true;lock.unlock();
  SearchSaveResult completed{job.epoch,{},job.best.bestPercent,job.attempts};
  try{
   if(job.writeBest)saveMacro(job.bestPath,job.best);
   auto saved=searchSessionJson(job.best,job.search,job.plan,job.seed,job.attempts,job.seconds);
   saved["learning"]=std::move(job.learning);saved["learning_player2"]=std::move(job.secondLearning);saved["failures"]=std::move(job.failures);saved["pending_guidance"]=job.guidance;
   saveJsonFile(job.sessionPath,saved,-1);
  }catch(std::exception const&e){completed.error=e.what();}
  lock.lock();m_result=std::move(completed);m_busy=false;lock.unlock();m_idle.notify_all();
 }
}
}
