//
//  lck_vector.h
//  usbmuxd2
//
//  Created by tihmstar on 17.08.19.
//  Copyright © 2019 tihmstar. All rights reserved.
//

#ifndef lck_vector_h
#define lck_vector_h

#include <vector>
#include <memory>
#include <sched.h>
#include <atomic>
#include <mutex>

template <class _container>
class lck_contrainer{
    static constexpr const uint32_t maxMembers = 0x10000;
    std::atomic<uint32_t> _members;
    std::mutex _enterLock;
    std::mutex _leaveLock;
public:
    _container _elems;

    inline lck_contrainer();
    inline ~lck_contrainer();

    //readonly access
    inline void addMember();
    inline void delMember();
    
    //write/modify access
    inline void lockMember();
    inline void unlockMember();
};

template <class _container>
lck_contrainer<_container>::lck_contrainer()
: _members(0)
{
    //
}

template <class _container>
lck_contrainer<_container>::~lck_contrainer(){
    while (_members) {
        _enterLock.lock();
    }
}


template <class _container>
void lck_contrainer<_container>::addMember(){
    while (true){
        if (_members.fetch_add(1) >= lck_contrainer::maxMembers){
            _members.fetch_sub(1);
            while (_members>=lck_contrainer::maxMembers)
                _enterLock.lock();
        }else{
            break;
        }
    }
}

template <class _container>
void lck_contrainer<_container>::delMember(){
    _members.fetch_sub(1);
    _enterLock.unlock();
    _leaveLock.unlock();
}

template <class _container>
void lck_contrainer<_container>::lockMember(){
    while (true){
        if (_members.fetch_add(lck_contrainer::maxMembers) >= lck_contrainer::maxMembers){
            _members.fetch_sub(lck_contrainer::maxMembers);
            while (_members>=lck_contrainer::maxMembers)
                _enterLock.unlock();
        }else{
            while (_members > lck_contrainer::maxMembers)
                _leaveLock.unlock(); //wait until all members are gone
            break;
        }
    }
}


template <class _container>
void lck_contrainer<_container>::unlockMember(){
    _members.fetch_sub(lck_contrainer::maxMembers);
    _enterLock.unlock();
    _leaveLock.unlock();
}


#endif /* lck_vector_h */
