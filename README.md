# 勾配ブースティングを利用した教師あり分かち書き

C++用のモデル（データを入力すると、結果が返ってくるもの）が構築できるので、任意の言語、例えばRuby等でも分かち書きを利用できる様になります  


## 理論

## 前処理
githubのレポジトリには最初から学習済みのデータを用意していますが、自分で必要なデータを学習させる場合には以下のステップで行います  

教師あり分かち書きなので、教師となる分かち書きの粒度を示したデータセットが必要となる  
例えば、mecabの生成する分かち書きを教師データとする例です  

映画.com様から取得した500MByte程度の映画のレビューデータを利用して、boosting-tree-tokenizerに学習させるデータセットを作成します  

ダウンロード(20GByte程度あります)
```console
$ cd misc
$ python3 downloader.py 
```

#### 1. ポジティブデータ、ネガティブデータセットの作成

以下の様なデータを作成します  

**ポジティブ**  
\[、今日は o 天気がい\]  
分かち書きがあり得る箇所にoが入ります  
**ネガティブ**  
\[ベネチア x ンマスク\]  
分かち書きとしてありえないところにxが入ります  

#### 2. スパースマトリックスの作成
特徴量の作り方として、周辺の単語とその距離の二つの軸で独立な特徴量とするため、まともにやろうとすると、大変高次元化して今います.  
そのため、スパースマトリックスとして表現するために、まず、特徴量に対してユニークなインデックスを付与します  
```console
$ python3 wakati.py --make_sparse
```
付与したインデックスをもとに、スパースマトリックスを組み立てます  
```console
$ python3 wakati.py --make_sparse2 
```

#### 3. train, testデータの作成
お使いのマシンのスペックに依存しますが、trainデータで100万, testデータで10万データセットを利用する際には、この様にしまします  
```console
$ head -n 1000000 ./misc/download/dataset.txt > train
$ tail -n 100000 ./misc/download/dataset.txt > test
```

## LightGBMでの学習
binary-classificationでの二値分類の問題としてみなします  
具体的には、ある文字と文字の間に、分かち書きすべきかどうかの確率値を計算します  
50%を超えると、分かち書きを有効にし、50%未満では分かち書きを行いません  

有名な勾配ブースティングライブラリにはXGBoostとLightGBMとCatBoostなどがあるのですが、スパースマトリックスの扱いやすさからLightGBMを使います  

### 1. LightGBMのインストール  
cmakeでビルドとインストールできます（要：GNU make, gcc, cmake）
```console
$ git clone https://github.com/Microsoft/LightGBM
$ cd LightGBM
$ mkdir build
$ cd build
$ cmake ..
$ make -j16
$ sudo make install
```
これでlightgbmコマンドがシステムに追加されました  

### 2. LightGBMで学習する
学習に使うパラメータを記述したconfがあるので、必要に応じでパラメータを変更して用いてください  
```console
$ lightgbm config=train.conf
```

