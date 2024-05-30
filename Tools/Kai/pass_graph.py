import sys
import os


def process_edge(line, graph):
    if line.find('Root ->') > 0:
        return
    if line.find('+++') == 0:
        edge = line[len('+++ insert edge'):]
        graph.append(edge)
        return
    if line.find('>>>') == 0:
        edge = line[len('>>> update edge'):]
        for old in graph:
            if edge[:-len('[color=blue]')] == old[:-len('[color=blue]')]:
                if edge != old:
                    graph.remove(old)
                    graph.append(edge)
                break
        return

def validate_edge(graph):
    ng = []
    cl = {}
    m = 0
    for edge in graph:
        m += 1
        ne = edge.replace('Root.', '')
        li = ne.find('[')
        pne = ne[:li].split(' -> ')
        lab = ne[li:]
        lab = lab.replace(' ', '_')
        lab = lab.replace('PreviewRendererSystemComponent_', '')
        eds = []
        for e in pne:
            if e.find('PreviewRendererSystemComponent') >= 0:
                xne = e.split('.')
                ne = 'Preview_' + '_'.join([xne[-2], xne[-1]])
                eds.append(ne)
            else:
                eds.append(e)
        ne = ' -> '.join(eds) + ' ' + lab
        if m % 2 == 0:
            ne = ne + ' [color=blue] [fontcolor=blue]'
        else:
            ne = ne + ' [color=maroon] [fontcolor=maroon]'
        ne = ne.replace('2DPass', '_2DPass')
        ne = ne.replace('.', '_')
        ne = ne.replace('$', '_')
        ng.append(ne)
        for n in ne[:ne.find('[')].strip().split(' -> '):
            if n.find('Preview_') == 0:
                cl[n] = '[fontcolor=green]'
            else:
                cl[n] = '[fontcolor=red]'
    #return ng
    for k, v in cl.items():
        ng.insert(0, k + ' ' + v + '\n')
    return ng

with open ('log.log', 'r') as fd:
    one_pipe_line = False
    one_pipe_done = False
    one_pipe_started = False
    two_pipe_line = False
    two_pipe_done = False
    two_pipe_started = False
    one_pipe_graph = []
    two_pipe_graph = []
    lines = fd.readlines()
    lnum = 0
    for line in lines:
        lnum += 1
        if one_pipe_done == False and line.find('### Main pipeline started!') >= 0:
            one_pipe_line = True
            if not one_pipe_started:
                one_pipe_started = True
                print('one pipe line start line {}'.format(lnum))
        elif one_pipe_line == True and line.find('<= FrameScheduler::PrepareProducers After FrameGraph node count') >= 0:
            one_pipe_line = False
            one_pipe_done = True
        elif two_pipe_done == False and line.find('### Test pipeline started!') >= 0:
            two_pipe_line = True
            if not two_pipe_started:
                two_pipe_started = True
                print('two pipe line start line {}'.format(lnum))
        elif two_pipe_line == True and line.find('<= FrameScheduler::PrepareProducers After FrameGraph node count') >= 0:
            two_pipe_line = False
            two_pipe_done = True
        elif one_pipe_done == True and two_pipe_done == True:
            break
        if one_pipe_line == True:
            process_edge(line, one_pipe_graph)
        if two_pipe_line == True:
            process_edge(line, two_pipe_graph)

    with open ('g_one_pipe_graph.log', 'w+') as ofd:
        ofd.write(' '.join(one_pipe_graph))
    with open ('g_two_pipe_graph.log', 'w+') as tfd:
        tfd.write(' '.join(two_pipe_graph))

    one_pipe_graph = validate_edge(one_pipe_graph)
    two_pipe_graph = validate_edge(two_pipe_graph)

    one_pipe_graph.insert(0, 'digraph {')
    two_pipe_graph.insert(0, 'digraph {')
    one_pipe_graph.append('}')
    two_pipe_graph.append('}')

    with open ('g_one_pipe_graph.dot', 'w+') as ofd:
        ofd.write(' '.join(one_pipe_graph))
    with open ('g_two_pipe_graph.dot', 'w+') as tfd:
        tfd.write(' '.join(two_pipe_graph))

    os.system('dot -Tsvg g_one_pipe_graph.dot -o g_one_pipe_graph.svg')
    os.system('dot -Tsvg g_two_pipe_graph.dot -o g_two_pipe_graph.svg')
