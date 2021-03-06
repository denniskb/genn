sims=(/vogels) # bsim ngpu (all sims)

echo "[" > results.json

run() {
	./../bin/genn-buildmodel.sh model.cc
	make
	echo $1
	eval "$1 >> results.json"
	echo -n "," >> results.json
}

for sim in ${sims[@]}
do
	# simtime
	for size in {250000000..3000000000..250000000}
	do
		echo "#pragma once" > inputs.h
		echo "double const NSYN = $size;" >> inputs.h
		run ".$sim"
	done
: '
	for size in {100000000..700000000..100000000}
	do
		run ".$sim --bench sim --gpu ${gpu[$igpu]} --model brunel+ --nsyn $size"
	done
'
done

echo -n "{}]" >> results.json