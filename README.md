# 勾配ブースティングを利用した教師あり形態素分析

辞書ファイルを必要とせず、C++用のモデル（データを入力すると、結果が返ってくるもの）が構築できるので、任意の言語、例えばRuby等でも分かち書きや品詞推定
を利用できる様になります  

## 名称について
色々な名称の形態素解析器がありますが、このプロジェクトから派生した今回の一連のコードを珊瑚(sango)としたいと思います  

理由は一部のサンゴが、任意の海流の流れ（とはいっても多分その海域での特徴とかあると思うんですが）を、任意の方向に整流することができます。これは、サンゴの中に複雑な微小な穴があって、穴はツリー状に内部で経路を構築しており、この穴をどこからでも海水が通ると、不思議なことに一定の方向性を持って海水が出てきます  

あたかも乱雑な情報から秩序と法則性を取り出すことができる勾配ブースティングの動作に似ていることから、この名前をつけたいと考えています  


## 理論 
ある文章の特定の文字と文字の間に分かち書きを行うべきかどうかの、二値分類問題として扱うことができると仮説を立てます  
周辺の”文字”の分布から、そこが分かち書きを行うのに適切なのかどうかを、なんらかの方法で人間が用意したデータを用い教師あり学習で学習します  
<div align="center">
  <img width="750px" src="https://user-images.githubusercontent.com/4949982/31882071-926d2d16-b820-11e7-810b-d40bd270cda7.png">
</div>
<div align="center"> 図1. 問題設定 </div>

この問題設定におけるベクトルを直列化して、スパースなベクトルとして表現すると、[LIVSVM](https://user-images.githubusercontent.com/4949982/31882071-926d2d16-b820-11e7-810b-d40bd270cda7.png)形式のフォーマットにすることができ、LightGBM, XGBoostなどで学習することが可能なフォーマットに変換できます  

品詞の推定も同様の理論で行うことができ、分かち書きした結果を利用して、再び周辺の”単語”の散らばりから予想します。  
単語も未知語である場合には不明ですが、周辺の既知の単語情報のみで品詞を推定することができます  


## 前処理
githubのレポジトリには最初から学習済みのデータを用意していますが、自分で必要なデータを学習させる場合には以下のステップで行います  

教師あり分かち書きなので、教師となる分かち書きの粒度を示したデータセットが必要となる  
例えば、mecabの生成する分かち書きを教師データとする例です  

映画.com様から取得した500MByte程度の映画のレビューデータを利用して、boosting-tree-tokenizer-sangoに学習させるデータセットを作成します  

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
特徴量の作り方として、周辺の文字、単語とその距離の二つの軸で独立な特徴量とするため、まともにやろうとすると、大変高次元化して今います.  
そのため、スパースマトリックスとして表現するために、まず、特徴量に対してユニークなインデックスを付与します  
```console
$ python3 wakati.py --make_sparse
$ python3 parts.py --make_sparse
```
付与したインデックスをもとに、スパースマトリックスを組み立てます  
```console
$ python3 wakati.py --make_sparse2 
$ python3 parts.py --make_sparse2
```

#### 3. train, testデータの作成
お使いのマシンのスペックに依存しますが、trainデータで100万, testデータで10万データセットを利用する際には、この様にしまします  
```console
$ head -n 50000000 ./misc/download/dataset.txt > train
$ head -n 3000000 ./misc/download/dataset_parts.txt > train_parts
$ tail -n 10000000 ./misc/download/dataset.txt > test
$ tail -n 100000 ./misc/download/dataset_parts.txt > test_parts
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
$ lightgbm config=train.parts.conf
```

## 学習したモデルで分かち書き、品詞推定をしてみる  

映画.comさんのレビューをランダムサンプルして適当に分かち書きしてみています  
```console
$ python3 intractive.py --test
```
出力はこの様になり、概ね、期待通りの分かち書きになっていることが確認できるかと思います  
```console
- 調子/に/乗り/過ぎ/て/いる/感/が/嫌い/だ/が/、/この/作品/は/見/て/よかっ/た
- はじめ/と/する/役者/さん/たち/の/演技/に/泣か/さ/れ/た/。/小林武史/の/音楽/も/素晴らしく/て/サントラ/を/買っ/て/しまっ/た/。/原作/も/読み/た
- 読ん/で/で/、/これ/を/見/た/が/とく/に/違和感/なく/見れ/た/。/多分/、/原作/は/もっと/色々命/と/の/引き換え/を/考え/て/た/気/も/する/けど/、/これ/は/これ/で/よい/。
- 映画/は/苦手/だっ/た/の/です/が/、/母/の/誘い/で/見/に/行き/まし/た/。/後半部分/が/良い/です/。/自分/で/も/なん/で/泣い/て/いる/か分から/ない/ほど/涙/が/出まし/た/。
- 目/が/真っ赤/だ/けど/大丈夫/？/！/と/言わ/れ/まし/た/.../。/今/に/なっ/て/、/あぁ/良い/映画/だっ/た/。/と/思っ/た/ので/レビュー/し/て/み/まし/た/。/命より/大切/な/もの/は/あり/ます/。
- スケジュール/が/合わ/なく/て/見/に/行け/なかっ/た/が/、/近場/の/IMAX上映館/が/今週末/から/箱割/を/大幅/に/カット/する/こと/が/わかり/、/慌て/て/見/に/行っ/た/。/IMAX上映/を/前提/と/し/た/映像/凄い/迫力/と/臨場感/だっ/た/が/、/人物/や/物語/は/平板/な/もので/、/特別/感動/も/なく/終了/。/欧州戦史/に対する/ある/程度/の/前提/知識/が/必要/だっ/た/の/かも/しれ/ない/と/思っ/て/パンフ/を/購入し/た/が/、/これ/が/凄い/情報量/だっ/た/。
- 今/に/なっ/て/、/あぁ/良い/映画/だっ/た/。/と/思っ/た/ので/レビュー/し/て/み/まし/た/。/命/より/大切なもの/は/あり/ます/。/
- 大切なもの/と/の/思い出/が/消え/た/中/で/１/人/で/生きる/なんて/つら/すぎ/ます/。/改めて/、/それ/に/気づけ/て/良かっ/た/。/周り/の/人/を/大切/に/しよう/と/思う/映画/。
```

品詞推定の例
実行時にオプションをつける必要があります
```console
$ python3 intractive.py --test --hinshi
```
出力はこのようになります
```console
本文/に/ネタバレ/が/あり/ます/。
本文 43 名詞-一般
に 33 助詞-格助詞-一般
ネタバレ 59 名詞-固有名詞-一般
が 33 助詞-格助詞-一般
あり 67 動詞-自立
ます 4 助動詞
。 12 記号-句点
```

## Pure C++で記述されたモデルを得る
まだLightGBMの実験的な機能だということですが、C\+\+で記述されたモデルを出力可能です。  
具体的には、決定木の関数オブジェクトのリストを返してくれて、自分でアンサンブルを組むことができるようになっているようです  
Pure C\+\+なので、C\+\+とバインディングできるRubyなどの言語は、別途MeCab等を利用しなくても辞書がなくても、形態素解析できるようになるという感じです  
(モデルをどう使うのかよくわからないのですが、知ってる人Twitter等でサンプルコード等教えていただけませんか。。。)

### C++形式のモデルを出力する
traon.confの以下のコメントアウトを外すと、C\+\+で直接コンパイルできて判別できるモデルができあがあります  
```console
convert_model=gbdt_prediction.cpp
convert_model_language=cpp
```

### feature_index.pklのC++化
pickle形式の特徴量の対応表はC\+\+には読めないので、cppのファイルに変換します  
```console
$ python3 intractive.py --make_cpp
```

## 実装

[https://github.com/GINK03/boosting-tree-tokenizer:Embed]

## 感想とか印象とか
このネタはもともと、何か論文書けこのやろうと詰められる機運が高まってきたので、何か自然言語処理で論文になるようなネタを探していた時にふと、勾配ブースティングの性質であればできるんじゃないのかな、しかもこの場合、辞書なしでできる、という気づきがあり、やってみました  
うまくいったので、なんらか形態素という人間が考える意味粒度を与えるデータセットさえあれば、傾向を一般化して、形態素解析として働きうることを示せたかと思います  
私のC\+\+力が足りないので、C\+\+の実装がうまくいかなったですが、仮に実装したとなると、モデルのみのインプリテーションで済むので、辞書ファイル等の更新が必要なくなるという大きなメリットがあります  

Twitterで報告したところ、[TinySegmenter(AdaBoostでの機械学習)](http://chasen.org/~taku/software/TinySegmenter/)という似たようなアプローチを知ったのですが、数パーセントですが、こっちの方が分かち書きの性能はよかったです。あと、TinySegmenterでは品詞の推定ができませんが、これはできることが強みでしょうか  

## ライセンス　
MIT
