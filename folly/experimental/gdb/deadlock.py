#!/usr/bin/env python3

from collections import defaultdict
import gdb
import re


class DiGraph(object):
    '''
    Adapted from networkx: http://networkx.github.io/
    Represents a directed graph. Edges can store (key, value) attributes.
    '''

    def __init__(self):
        # Map of node -> set of nodes
        self.adjacency_map = {}
        # Map of (node1, node2) -> map string -> arbitrary attribute
        # This will not be copied in subgraph()
        self.attributes_map = {}

    def neighbors(self, node):
        return self.adjacency_map.get(node, set())

    def edges(self):
        edges = []
        for node, neighbors in self.adjacency_map.items():
            for neighbor in neighbors:
                edges.append((node, neighbor))
        return edges

    def nodes(self):
        return self.adjacency_map.keys()

    def attributes(self, node1, node2):
        return self.attributes_map[(node1, node2)]

    def add_edge(self, node1, node2, **kwargs):
        if node1 not in self.adjacency_map:
            self.adjacency_map[node1] = set()
        if node2 not in self.adjacency_map:
            self.adjacency_map[node2] = set()
        self.adjacency_map[node1].add(node2)
        self.attributes_map[(node1, node2)] = kwargs

    def remove_node(self, node):
        self.adjacency_map.pop(node, None)
        for _, neighbors in self.adjacency_map.items():
            neighbors.discard(node)

    def subgraph(self, nodes):
        graph = DiGraph()
        for node in nodes:
            for neighbor in self.neighbors(node):
                if neighbor in nodes:
                    graph.add_edge(node, neighbor)
        return graph

    def node_link_data(self):
        '''
        Returns the graph as a dictionary in a format that can be
        serialized.
        '''
        data = {
            'directed': True,
            'multigraph': False,
            'graph': {},
            'links': [],
            'nodes': [],
        }

        # Do one pass to build a map of node -> position in nodes
        node_to_number = {}
        for node in self.adjacency_map.keys():
            node_to_number[node] = len(data['nodes'])
            data['nodes'].append({'id': node})

        # Do another pass to build the link information
        for node, neighbors in self.adjacency_map.items():
            for neighbor in neighbors:
                link = self.attributes_map[(node, neighbor)].copy()
                link['source'] = node_to_number[node]
                link['target'] = node_to_number[neighbor]
                data['links'].append(link)
        return data


def strongly_connected_components(G):  # noqa: C901
    '''
    Adapted from networkx: http://networkx.github.io/
    Parameters
    ----------
    G : DiGraph
    Returns
    -------
    comp : generator of sets
        A generator of sets of nodes, one for each strongly connected
        component of G.
    '''
    preorder = {}
    lowlink = {}
    scc_found = {}
    scc_queue = []
    i = 0  # Preorder counter
    for source in G.nodes():
        if source not in scc_found:
            queue = [source]
            while queue:
                v = queue[-1]
                if v not in preorder:
                    i = i + 1
                    preorder[v] = i
                done = 1
                v_nbrs = G.neighbors(v)
                for w in v_nbrs:
                    if w not in preorder:
                        queue.append(w)
                        done = 0
                        break
                if done == 1:
                    lowlink[v] = preorder[v]
                    for w in v_nbrs:
                        if w not in scc_found:
                            if preorder[w] > preorder[v]:
                                lowlink[v] = min([lowlink[v], lowlink[w]])
                            else:
                                lowlink[v] = min([lowlink[v], preorder[w]])
                    queue.pop()
                    if lowlink[v] == preorder[v]:
                        scc_found[v] = True
                        scc = {v}
                        while (
                            scc_queue and preorder[scc_queue[-1]] > preorder[v]
                        ):
                            k = scc_queue.pop()
                            scc_found[k] = True
                            scc.add(k)
                        yield scc
                    else:
                        scc_queue.append(v)


def simple_cycles(G):  # noqa: C901
    '''
    Adapted from networkx: http://networkx.github.io/
    Parameters
    ----------
    G : DiGraph
    Returns
    -------
    cycle_generator: generator
       A generator that produces elementary cycles of the graph.
       Each cycle is represented by a list of nodes along the cycle.
    '''

    def _unblock(thisnode, blocked, B):
        stack = set([thisnode])
        while stack:
            node = stack.pop()
            if node in blocked:
                blocked.remove(node)
                stack.update(B[node])
                B[node].clear()

    # Johnson's algorithm requires some ordering of the nodes.
    # We assign the arbitrary ordering given by the strongly connected comps
    # There is no need to track the ordering as each node removed as processed.
    # save the actual graph so we can mutate it here
    # We only take the edges because we do not want to
    # copy edge and node attributes here.
    subG = G.subgraph(G.nodes())
    sccs = list(strongly_connected_components(subG))
    while sccs:
        scc = sccs.pop()
        # order of scc determines ordering of nodes
        startnode = scc.pop()
        # Processing node runs 'circuit' routine from recursive version
        path = [startnode]
        blocked = set()  # vertex: blocked from search?
        closed = set()  # nodes involved in a cycle
        blocked.add(startnode)
        B = defaultdict(set)  # graph portions that yield no elementary circuit
        stack = [(startnode, list(subG.neighbors(startnode)))]
        while stack:
            thisnode, nbrs = stack[-1]
            if nbrs:
                nextnode = nbrs.pop()
                if nextnode == startnode:
                    yield path[:]
                    closed.update(path)
                elif nextnode not in blocked:
                    path.append(nextnode)
                    stack.append((nextnode, list(subG.neighbors(nextnode))))
                    closed.discard(nextnode)
                    blocked.add(nextnode)
                    continue
            # done with nextnode... look for more neighbors
            if not nbrs:  # no more nbrs
                if thisnode in closed:
                    _unblock(thisnode, blocked, B)
                else:
                    for nbr in subG.neighbors(thisnode):
                        if thisnode not in B[nbr]:
                            B[nbr].add(thisnode)
                stack.pop()
                path.pop()
        # done processing this node
        subG.remove_node(startnode)
        H = subG.subgraph(scc)  # make smaller to avoid work in SCC routine
        sccs.extend(list(strongly_connected_components(H)))


def find_cycle(graph):
    '''
    Looks for a cycle in the graph. If found, returns the first cycle.
    If nodes a1, a2, ..., an are in a cycle, then this returns:
        [(a1,a2), (a2,a3), ... (an-1,an), (an, a1)]
    Otherwise returns an empty list.
    '''
    cycles = list(simple_cycles(graph))
    if cycles:
        nodes = cycles[0]
        nodes.append(nodes[0])
        edges = []
        prev = nodes[0]
        for node in nodes[1:]:
            edges.append((prev, node))
            prev = node
        return edges
    else:
        return []


def print_cycle(graph, lwp_to_thread_id, cycle):
    '''Prints the threads and mutexes involved in the deadlock.'''
    for (m, n) in cycle:
        print(
            'Thread %d (LWP %d) is waiting on mutex (0x%016x) held by '
            'Thread %d (LWP %d)' % (
                lwp_to_thread_id[m], m, graph.attributes(m, n)['mutex'],
                lwp_to_thread_id[n], n
            )
        )


def get_thread_info():
    '''
    Returns a pair of:
    - map of LWP -> thread ID
    - set of blocked threads LWP
    '''
    # LWP -> thread ID
    lwp_to_thread_id = {}

    # Set of threads blocked on mutexes
    blocked_threads = set()

    output = gdb.execute('info threads', from_tty=False, to_string=True)
    lines = output.strip().split('\n')[1:]
    regex = re.compile(r'[\s\*]*(\d+).*Thread.*\(LWP (\d+)\).*')
    for line in lines:
        thread_id = int(regex.match(line).group(1))
        thread_lwp = int(regex.match(line).group(2))
        lwp_to_thread_id[thread_lwp] = thread_id
        if '__lll_lock_wait' in line:
            blocked_threads.add(thread_lwp)

    return (lwp_to_thread_id, blocked_threads)


def get_mutex_owner_and_address(lwp_to_thread_id, thread_lwp):
    '''
    Finds the thread holding the mutex that this thread is blocked on.
    Returns a pair of (lwp of thread owning mutex, mutex address),
    or (None, None) if not found.
    '''
    # Go up the stack to the pthread_mutex_lock frame
    gdb.execute(
        'thread %d' % lwp_to_thread_id[thread_lwp],
        from_tty=False,
        to_string=True
    )
    gdb.execute('frame 1', from_tty=False, to_string=True)

    # Get the owner of the mutex by inspecting the internal
    # fields of the mutex.
    try:
        mutex_info = gdb.parse_and_eval('mutex').dereference()
        mutex_owner_lwp = int(mutex_info['__data']['__owner'])
        return (mutex_owner_lwp, int(mutex_info.address))
    except gdb.error:
        return (None, None)


class Deadlock(gdb.Command):
    '''Detects deadlocks'''

    def __init__(self):
        super(Deadlock, self).__init__('deadlock', gdb.COMMAND_NONE)

    def invoke(self, arg, from_tty):
        '''Prints the threads and mutexes in a deadlock, if it exists.'''
        lwp_to_thread_id, blocked_threads = get_thread_info()

        # Nodes represent threads. Edge (A,B) exists if thread A
        # is waiting on a mutex held by thread B.
        graph = DiGraph()

        # Go through all the blocked threads and see which threads
        # they are blocked on, and build the thread wait graph.
        for thread_lwp in blocked_threads:
            mutex_owner_lwp, mutex_address = get_mutex_owner_and_address(
                lwp_to_thread_id, thread_lwp
            )
            if mutex_owner_lwp and mutex_address:
                graph.add_edge(thread_lwp, mutex_owner_lwp,
                               mutex=mutex_address)

        # A deadlock exists if there is a cycle in the graph.
        cycle = find_cycle(graph)
        if cycle:
            print('Found deadlock!')
            print_cycle(graph, lwp_to_thread_id, cycle)
        else:
            print(
                'No deadlock detected. '
                'Do you have debug symbols installed?'
            )


def load():
    # instantiate the Deadlock command
    Deadlock()
    print('Type "deadlock" to detect deadlocks.')


def info():
    return 'Detect deadlocks'


if __name__ == '__main__':
    load()
