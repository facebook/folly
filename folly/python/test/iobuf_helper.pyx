# distutils: language=c++

from folly.iobuf cimport from_unique_ptr, createChain, IOBuf

def get_empty_chain():
    return from_unique_ptr(createChain(1024, 128))

def make_chain(data):
    cdef IOBuf head = data.pop(0)
    # Make a new chain
    head = from_unique_ptr(head.c_clone())
    cdef IOBuf tbuf
    cdef IOBuf last = head
    for tbuf in data:
        last._this.appendChain(tbuf.c_clone())
        last = last.next
    return head
