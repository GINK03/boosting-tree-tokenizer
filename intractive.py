import os
import sys
import MeCab
import json
import pickle
if '--check' in sys.argv:
  probs = [float(line.strip()) for line in open('./prediction.txt')]
  origs = [line.strip() for line in open('./test')]
  for prob, orig in zip(probs,origs):
    print(prob, orig)

if '--test' in sys.argv:
  idf_index = pickle.loads(open('./misc/download/idf_index.pkl', 'rb').read() )
  for line in open('./misc/download/reviews.json'):
    obj = json.loads( line.strip() )
    title = obj['title']
    review = obj['review']
    chars = list(review)
    # slicing 
    data = []
    for i in range(len(chars) - 8):
      builder = "0.5 "
      for index, char in enumerate(chars[i:i+8]):
        keys = '%d%s'%(index,char)
        if idf_index.get(keys) is not None:
          builder  += str(idf_index.get(keys)) + ":1.0 " 
        else:
          ...
      #print(builder)
      data.append( builder.strip() )

    open('data/{}.data'.format(title), 'w').write('\n'.join(data) )
    open('data/{}.orig'.format(title), 'w').write( review )

    os.system('lightgbm config=predict.conf data=data/{}.data'.format(title))
    probs = [float(line.strip()) for line in open('./prediction.txt')]
    origs = [line.strip() for line in open('data/{}.data'.format(title))]
    #for prob, orig in zip(probs,origs):
    #   print(prob, orig)
    #print(probs)
    
    try:
      for i in range(len(chars) - 8):
        prob = probs[i] 
        char = chars[i+3]
        print(char, end='')
        if prob > 0.5:
          print('/', end='') 
      print()
    except IndexError as e:
      print()
     
