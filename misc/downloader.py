import os
import sys

domain = 'http://121.2.69.245:10001'

# ローカルサーバからダウンロードする場合
if '--local' in sys.argv:
  domain = 'http://192.168.15.21:10001'

if not os.path.exists('download/dataset.txt'):
  os.system('wget {}/open/boosting-tree-tokenizer/dataset.txt'.format(domain))
  os.system('mv dataset.txt download/dataset.txt')

if not os.path.exists('download/dataset_raw.txt'):
  os.system('wget {}/open/boosting-tree-tokenizer/dataset_raw.txt'.format(domain))
  os.system('mv dataset_raw.txt download/dataset_raw.txt')

if not os.path.exists('download/reviews.json'):
  os.system('wget {}/open/boosting-tree-tokenizer/reviews.json'.format(domain))
  os.system('mv reviews.json download/reviews.json')

if not os.path.exists('download/train'):
  os.system('wget {}/open/boosting-tree-tokenizer/train'.format(domain))
  os.system('mv train download/train')

if not os.path.exists('download/test'):
  os.system('wget {}/open/boosting-tree-tokenizer/test'.format(domain))
  os.system('mv test download/test')

if not os.path.exists('download/idf_index.pkl'):
  os.system('wget {}/open/boosting-tree-tokenizer/idf_index.pkl'.format(domain))
  os.system('mv idf_index.pkl download/idf_index.pkl')
