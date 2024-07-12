# feps_subs, FepsSubs, feps_substitute, FepsSubstitute
cp -r feps_subs $1;
cd $1;
find . -not -type d | xargs sed -i "s/FepsSubstitute/$4/g";
find . -not -type d | xargs sed -i "s/feps_substitute/$3/g";
find . -not -type d | xargs sed -i "s/FepsSubs/$2/g";
find . -not -type d | xargs sed -i "s/feps_subs/$1/g";
find . -not -type d | xargs rename "s/FepsSubstitute/$4/";
find . -type d | xargs rename "s/feps_subs/$1/"
