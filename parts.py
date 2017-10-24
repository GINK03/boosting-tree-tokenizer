import MeCab
import json
import os
import sys
import pickle
import numpy as np

if '--make_data' in sys.argv:
  mm = MeCab.Tagger('-Ochasen')
  mw = MeCab.Tagger('-Owakati')
  for line in open('./misc/download/reviews.json'):
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
        head = terms[i:i+5]
        target = terms[i+5]
        part = parts[i+5-1]
      except IndexError as e:
        continue
      if target != part[0]: 
        continue
      tail = terms[i+6:i+10]
      print( ' '.join(head), target, ' '.join(tail), part[1] )

if '--make_sparse' in sys.argv:
  idfs = set()
  for enum, line in enumerate( open('./misc/download/dataset_raw.txt') ):
    line = line.strip()
    if enum%100000 == 0:
      print('now iter', enum, line, 'size', len(idfs))
    flag = 0.0 if ' x ' in line else 1.0
    line = line.replace(' x ', '').replace(' o ', '')
    for index, char in enumerate(list(line)):
      idf = '%d%s'%(index,char)
      idfs.add(idf)
  idf_index = {} 
  for index, idf in enumerate(list(idfs)):
    idf_index[idf] = index
  open('./misc/download/idf_index.pkl', 'wb').write( pickle.dumps(idf_index) )

if '--make_sparse2' in sys.argv:
  idf_index = pickle.loads(open('./misc/download/idf_index.pkl', 'rb').read( ) )
  f = open('./misc/download/dataset.txt', 'w')
  for enum, line in enumerate( open('./misc/download/dataset_raw.txt') ):
    line = line.strip()
    if enum%100000 == 0:
      print('now iter', enum, line)
    flag = 0.0 if ' x ' in line else 1.0
    line = line.replace(' x ', '').replace(' o ', '')
    sparse = ' '.join( ['%d:1.0'%idf_index['%d%s'%(index,char)] for index, char in enumerate(list(line))] )
    data = '%0.2f %s'%(flag, sparse)
    f.write(data+ '\n')
