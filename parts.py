import MeCab
import json
import os
import sys
import pickle
import numpy as np

if '--make_data' in sys.argv:
  mm = MeCab.Tagger('-Ochasen')
  mw = MeCab.Tagger('-Owakati')
  for index, line in enumerate( open('./misc/download/reviews.json') ):
    if index%10000 == 0:
      print('now iter', index, file=sys.stderr)
    obj = json.loads( line )
    text = '********' + obj['review'] + '************'
    parts = mm.parse( text ).strip().split('\n') 
    parts.pop(0)
    parts.pop()
    parts = [(p[0], p[3])for p in [ [e for e in p.split('\t')] for p in parts] ]
    #print(parts)
    terms =  mw.parse( text ).strip().split() 
    #print(terms)
    for i in range(len(terms)-10):
      try:
        head = terms[i:i+4]
        target = terms[i+4]
        part = parts[i+4]
      except IndexError as e:
        continue
      if target != part[0]: 
        continue
      tail = terms[i+6:i+10]
      print( ' '.join(head), target, ' '.join(tail), part[1] )

if '--make_sparse' in sys.argv:
  aterms = set()
  parts = set()
  for enum, line in enumerate( open('./misc/download/dataset_parts_raw.txt') ):
    line = line.strip()
    if enum%100000 == 0:
      print('now iter', enum, line, 'size', len(aterms))
    #if enum > 100000:
    #  break
    terms = line.split()
    part = terms.pop()
    [ aterms.add( term ) for term in terms ]
    parts.add(part)

  parts_index = {}
  for index, part in enumerate(list(parts)): 
    parts_index[part] = index
  aterm_index = {}
  for index, term in enumerate(list(aterms)): 
    aterm_index[term] = index
  open('./misc/download/parts_index.pkl', 'wb').write( pickle.dumps(parts_index) )
  open('./misc/download/aterm_index.pkl', 'wb').write( pickle.dumps(aterm_index) )

if '--make_sparse2' in sys.argv:
  parts_index = pickle.loads(open('./misc/download/parts_index.pkl', 'rb').read( ) )
  aterm_index = pickle.loads(open('./misc/download/aterm_index.pkl', 'rb').read( ) )
  f = open('./misc/download/dataset_parts.txt', 'w')
  for enum, line in enumerate( open('./misc/download/dataset_parts_raw.txt') ):
    line = line.strip()
    if enum%100000 == 0:
      print('vetorizing now iter', enum, line)
    terms = line.split()
    part_index = parts_index[terms.pop()]
    dataset = ' '.join( ['%d:%s'%(aterm_index[term], "1.0") for index, term in enumerate(terms) if aterm_index.get(term) is not None] )
    dataset = str(part_index) + ' ' + dataset
    f.write(dataset + '\n')
