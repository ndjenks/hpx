//  Copyright (c) 2007-2008 Hartmut Kaiser
// 
//  Distributed under the Boost Software License, Version 1.0. (See accompanying 
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#if !defined(HPX_LCOS_CONDITION_JUL_04_2008_0125PM)
#define HPX_LCOS_CONDITION_JUL_04_2008_0125PM

#include <boost/detail/atomic_count.hpp>
#include <boost/lockfree/fifo.hpp>

// Description of the condition algorithm is explained here:
// http://lists.boost.org/Archives/boost/2006/09/110367.php
//
// The algorithm is as follows: 
//
// waiting_list_node: 
//     waiting_list_node* next, prev 
//     HANDLE thread_handle 
//     bool notified 
// 
// waiting_list: doubly-linked list of waiting_list_node 
// gate: mutex 
// 
// init(): 
//     waiting_list.next=waiting_list.prev=&waiting_list 
//     init mutex 
// 
// wait(external_mutex, timeout): 
//     create a new waiting_list_node 
//     new_node.thread_handle=thread handle for this thread 
//     new_node.prev=&waiting_list 
//     lock(gate) 
//     new_node.next=waiting_list.next 
//     waiting_list.next=&new_node 
//     new_node.next->prev=&new_node 
//     unlock(external_mutex) 
//     unlock(gate) 
// 
//     // Any APC will break the sleep, so keep sleeping until we've been 
//     // notified, or we've timed out 
//     while(!atomic_read(new_node.notified) 
//         && SleepEx(milliseconds_until(timeout), true)==WAIT_IO_COMPLETION); 
// 
//     lock(gate) 
//     unlink(new_node) 
//     unlock(gate) 
//     lock(external_mutex) 
//     return new_node.notified // did we timeout, or were we notified? 
// 
// unlink(node) 
//     // unlink the node from the list 
//     node.next->prev=new_node.prev 
//     node.prev->next=new_node.next 
//     node.next=node.prev=&node 
// 
// notify_and_unlink_entry(node) 
//     atomic_set(node->notified,true) 
//     unlink(node) 
//     // wake the node's thread by queueing an APC 
//     // the APC func doesn't have to do anything to wake the thread 
//     QueueUserAPC(NOP(),node->thread_handle) 
// 
// notify_one() 
//     lock(gate) 
//     if(waiting_list.prev==&waiting_list) do nothing 
//     else 
//         notify_and_unlink_entry(waiting_list.prev) 
//     unlock(gate) 
// 
// notify_all() 
//     create a waiting_list_node for new_list 
//     lock(gate) 
//     // transfer the existing list over to new_list 
//     new_list.prev=waiting_list.prev 
//     new_list.next=waiting_list.next 
//     new_list.next->prev=&new_list 
//     new_list.prev->next=&new_list 
//     // the waiting_list is now empty 
//     waiting_list.next=waiting_list.prev=&waiting_list 
//     unlock(gate) // noone has to wait for us any more 
// 
//     while(new_list.prev!=&new_list) // loop until the captured list is empty 
//         notify_and_unlink_entry(new_list.prev) 

///////////////////////////////////////////////////////////////////////////////
namespace hpx { namespace lcos { namespace detail 
{
    // A condition can be used to synchronize an arbitrary number of threads, 
    // blocking all of the entering threads until either a single one or all of 
    // them get notified (released)
    class condition : public lcos::base_lco
    {
    public:
        // This is the component id. Every component needs to have an embedded
        // enumerator 'value' which is used by the generic action implementation
        // to associate this component with a given action.
        enum { value = components::component_condition };

        condition()
        {}

        void wait(threads::thread_self& self)
        {
            queue_.enqueue(self.get_thread_id());
            self.yield(threads::suspended);
        }

        void notify_one(threads::thread_self& self)
        {
            thread_id_type id = 0;
            if (queue_.dequeue(&id))
                threads::set_state(self, id, threads::pending);
        }

        void notify_all(threads::thread_self& self)
        {
            thread_id_type id = 0;
            while (queue_.dequeue(&id))
                threads::set_state(self, id, threads::pending);
        }

        // standard LCO action implementations
        threads::thread_state set_event (
            threads::thread_self& self, applier::applier&)
        {
            notify_one(self);
        }

        threads::thread_state set_error (
            threads::thread_self& self, applier::applier&
            hpx::error code, std::string msg)
        {
            HPX_THROW_EXCEPTION(code, msg);
        }

    private:
        boost::lockfree::fifo<thread_id_type> queue_;
    };

}}}

///////////////////////////////////////////////////////////////////////////////
namespace hpx { namespace lcos 
{
    class condition
    {
    private:
        typedef detail::condition wrapped_type;
        typedef components::managed_component_base<wrapped_type> wrapping_type;

    public:
        condition()
          : impl_(new wrapping_type(new wrapped_type()))
        {}

        void wait(threads::thread_self& self)
        {
            impl_->wait(self);
        }

        void notify_one(threads::thread_self& self)
        {
            impl_->notify_one(self);
        }

        void notify_all(threads::thread_self& self)
        {
            impl_->notify_one(self);
        }

    private:
        boost::intrusive_ptr<wrapping_type> impl_;
    };

}}

#endif

