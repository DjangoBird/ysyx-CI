set DESIGN [lindex $argv 0]
set LIBERTY [lindex $argv 1]
set PERIOD_PS [lindex $argv 2]
set OUTPUT [lindex $argv 3]
set RTL_FILES [string map {"\"" ""} [lindex $argv 4]]
set RESULT_DIR [file dirname $OUTPUT]

yosys -import

foreach file $RTL_FILES {
  read_verilog -sv $file
}

synth -top $DESIGN -flatten
dfflibmap -liberty $LIBERTY
abc -D $PERIOD_PS -liberty $LIBERTY
setundef -zero
hilomap -singleton -hicell LOGIC1_X1 Z -locell LOGIC0_X1 Z
clean -purge

read_liberty -lib $LIBERTY
tee -o $RESULT_DIR/synth_check.txt check -mapped
tee -o $RESULT_DIR/synth_stat.txt stat -liberty $LIBERTY
splitnets -format __v -ports
clean -purge
write_verilog -noattr -noexpr -nohex -nodec $OUTPUT
