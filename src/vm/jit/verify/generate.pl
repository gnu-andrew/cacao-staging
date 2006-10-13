#!/usr/bin/perl
# src/vm/jit/verify/generate.pl - verifier generator
#
# Copyright (C) 1996-2005, 2006 R. Grafl, A. Krall, C. Kruegel,
# C. Oates, R. Obermaisser, M. Platter, M. Probst, S. Ring,
# E. Steiner, C. Thalinger, D. Thuernbeck, P. Tomsich, C. Ullrich,
# J. Wenninger, Institut f. Computersprachen - TU Wien
#
# This file is part of CACAO.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2, or (at
# your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.
#
# Contact: cacao@cacaojvm.org
#
# Authors: Edwin Steiner
#
# Changes:
#
# $Id$


use strict;
use warnings;
use Getopt::Long;
use IO::File;

#################### options

my $opt_icmdtable;
my $opt_table = 0;
my $opt_stack = 0;
my $opt_variables = 0;
my $opt_help = 0;

my $usage = <<"END_USAGE";
Usage:
    $0 --icmdtable FILE  { --table | --stack | --variables }

Options:
    --icmdtable FILE         read ICMD table from FILE
    --table                  print ICMD table
    --stack                  generate stackbased verifier
    --variables              generate variablesbased verifier

    Please specify exactly one of --table, --stack, or --variables.

END_USAGE

my $result = GetOptions("icmdtable=s" => \$opt_icmdtable,
						"table"       => \$opt_table,
						"stack"       => \$opt_stack,
						"variables"   => \$opt_variables,
						"help|h|?"    => \$opt_help,
		);

$result or die "$0: invalid options\n";

if ($opt_help) {
	print $usage;
	exit 0;
}

if (!defined($opt_icmdtable)
	|| ($opt_table + $opt_stack + $opt_variables != 1))
{
	print STDERR $usage;
	exit 1;
}

#################### constants

my $VERIFY_C = 'src/vm/jit/verify/icmds.c';
my $TYPECHECK_STACKBASED_INC = 'src/vm/jit/verify/typecheck-stackbased-gen.inc';
my $TYPECHECK_VARIABLESBASED_INC = 'src/vm/jit/verify/typecheck-variablesbased-gen.inc';

my $TRACE = 1;

my @basictypes = qw(A I F L D R); # XXX remove R?
my %basictypes = map { $_ => 1 } @basictypes;

my %slots = (
		1 => 1,
		2 => 2,
		A => 1,
		I => 1,
		F => 1,
		L => 2,
		D => 2,
		R => 1, # XXX remove?
);

my %cacaotypes = (
		A => 'TYPE_ADR',
		I => 'TYPE_INT',
		F => 'TYPE_FLT',
		L => 'TYPE_LNG',
		D => 'TYPE_DBL',
		R => 'TYPE_RET', # XXX remove?
);

my @superblockend = qw(END GOTO JSR RET TABLE LOOKUP);
my %superblockend = map { $_ => 1 } @superblockend;

my @validstages = qw( -- -S S+ );
my %validstages = map { $_ => 1 } @validstages;

#################### global variables

my @icmds;
my %icmds;
my %icmdtraits;

my $codeline;
my $codefile;

#################### subs

sub parse_verify_code
{
	my ($filename, $select) = @_;

	my $file = IO::File->new($filename) or die "$0: could not open: $filename";
	my $icmd;
	my $codeprops;
	my $ignore = 0;

	while (<$file>) {
		last if /^\s*\/\*\s*{START_OF_CODE}\s*\*\/\s*$/;
	}

	while (<$file>) {
		last if /^\s*\/\*\s*{END_OF_CODE}\s*\*\/\s*$/;

		if (/^case/) {
			unless (/^case \s+ (\w+) \s* :
					 \s* ( \/\* \s* {(STACK|VARIABLES)BASED} \s* \*\/ )?
					 \s* $/x)
			{
				die "$0: invalid case line: $filename:$.: $_";
			}
			my ($n, $unused, $tag) = ($1, $2, $3);

			defined($icmds{$n}) or die "$0: unknown ICMD: $filename:$.: $_";

			if (defined($tag) && $tag ne $select) {
				$ignore = 1;
			}
			else {
				$ignore = 0;

				my $code = [];
				$codeprops = {};

				if (defined($icmd)) {
					$code = $icmd->{VERIFYCODE};
					$codeprops = $icmd->{VERIFYCODEPROPS};
				}

				$icmd = $icmds{$n};
				$icmd->{VERIFYCODE} = $code;
				$icmd->{VERIFYCODELINE} = $. + 1;
				$icmd->{VERIFYCODEFILE} = $filename;
				$icmd->{VERIFYCODEPROPS} = $codeprops;
			}
		}
		elsif ($ignore) {
			if (/^\s*break\s*;\s*$/
				|| /^\s*goto\s+(\w+)\s*;\s*$/)
			{
				$ignore = 0;
			}
		}
		elsif (defined($icmd)) {
			if (/^\s*break\s*;\s*$/) {
				undef $icmd;
			}
			elsif (/^\s*goto\s+(\w+)\s*;\s*$/) {
				$icmd->{GOTOLABEL} = $1;
				undef $icmd;
			}
			else {
				if (/\{RESULTNOW\}/) {
					$codeprops->{RESULTNOW} = 1;
				}

				if (/\S/ || scalar @{$icmd->{VERIFYCODE}} != 0) {
					push @{$icmd->{VERIFYCODE}}, $_;
				}
			}
		}
		else {
			next if /^\s*$/;
			next if /^\s*\/\*.*\*\/\s*$/;

			die "$0: cannot handle code line outside case: $filename:$.: $_";
		}
	}
}

sub fill
{
	my ($str, $len) = @_;

	return $str . (' ' x ($len - length($str)));
}

sub add_flag
{
	my ($flags, $name) = @_;

	if ($$flags eq '0') {
		$$flags = $name;
	}
	else {
		$$flags .= '|'.$name;
	}
}

sub post_process_icmds
{
	my ($file) = @_;

	my $maxnamelen = 0;
	my $maxfullnamelen = 0;
	my $maxactionlen = 0;
	my $maxflagslen = 0;

	for my $icmdname (@icmds) {
		my $icmd = $icmds{$icmdname};

		{
			my $action = $icmd->{ACTION};
			my @variants = split (/\s*\|\s*/, $action);

			$icmd->{VARIANTS} = [];

			for my $v (@variants) {
				$v =~ /^(.*?)\s*--\s*(.*)$/ or die "invalid action: $_";
				my ($in, $out) = ($1, $2);

				my @in;
				my @out;
				if ($in =~ /\s/) {
					@in = split /\s*/, $in;
				}
				else {
					@in = split //, $in;
				}
				if ($out =~ /\s/) {
					@out = split /\s*/, $out;
				}
				else {
					@out = split //, $out;
				}
				my $invars = scalar @in;
				my $outvars = scalar @out;

				my $var = {};
				push @{$icmd->{VARIANTS}}, $var;

				$var->{IN} = \@in;
				$var->{OUT} = \@out;

				$icmd->{INVARS} = $invars;
				$icmd->{OUTVARS} = $outvars;

				my $inslots = 0;
				my $outslots = 0;
				for (@in) {
					my $slots = $slots{$_};
					defined($slots) or undef $inslots, last;
					$inslots += $slots;
				}
				for (@out) {
					my $slots = $slots{$_};
					defined($slots) or undef $outslots, last;
					$outslots += $slots;
				}

				$var->{INSLOTS} = $inslots;
				$var->{OUTSLOTS} = $outslots;

				if (defined($inslots)) {
					if (!defined($icmd->{MININSLOTS}) || $inslots < $icmd->{MININSLOTS}) {
						$icmd->{MININSLOTS} = $inslots;
					}

					if (exists $icmd->{INSLOTS}) {
						if ($icmd->{INSLOTS} != $inslots) {
							$icmd->{INSLOTS} = undef;
						}
					}
					else {
						$icmd->{INSLOTS} = $inslots;
					}
				}
				else {
					$icmd->{INSLOTS} = undef;
					$icmd->{MININSLOTS} = undef;
				}

				if (defined($outslots)) {
					if (exists $icmd->{OUTSLOTS}) {
						if (defined($icmd->{OUTSLOTS}) && $icmd->{OUTSLOTS} != $outslots) {
							$icmd->{OUTSLOTS} = undef;
						}
					}
					else {
						$icmd->{OUTSLOTS} = $outslots;
					}
				}
				else {
					$icmd->{OUTSLOTS} = undef;
				}

				if ($outvars == 0 || $outvars == 1) {
					my $df = $invars . '_TO_' . $outvars;
					if (defined($icmd->{DATAFLOW})) {
						if ($icmd->{DATAFLOW} =~ /^\d_TO_\d$/) {
							$icmd->{DATAFLOW} eq $df
								or die "$0: dataflow not consistent with action: "
											.$icmd->{FULLNAME}."\n";
						}
					}
					else {
						$icmd->{DATAFLOW} = $df;
					}
				}

				if (@out == 1) {
					if ($basictypes{$out[0]}) {
						$icmd->{BASICOUTTYPE} = $out[0];
					}
				}
			}
			$icmd->{ACTION} =~ s/\s//g;
		}

		$maxfullnamelen = length($icmdname) if length($icmdname) > $maxfullnamelen;
		$maxnamelen = length($icmd->{NAME}) if length($icmd->{NAME}) > $maxnamelen;
		$maxactionlen = length($icmd->{ACTION}) if length($icmd->{ACTION}) > $maxactionlen;

		my $parent;

		$icmd->{STAGE} = '  ' unless defined($icmd->{STAGE});

		if ($icmdname =~ /^(.*)CONST$/ && defined($icmds{$1})) {
			$parent = $icmds{$1};
		}

		if (!defined($icmd->{DATAFLOW}) && defined($parent)) {
			if ($parent->{DATAFLOW} =~ /(\d)_TO_(\d)/) {
				$1 >= 0 or die "$0: cannot derive data-flow: $icmdname from ".$parent->{FULLNAME};
				$icmd->{DATAFLOW} = ($1-1).'_TO_'.$2;
			}
		}

		if (!defined($icmd->{BASICOUTTYPE}) && defined($parent)) {
			$icmd->{BASICOUTTYPE} = $parent->{BASICOUTTYPE};
		}

		if (defined($icmd->{INSLOTS}) && defined($icmd->{OUTSLOTS})) {
			$icmd->{STACKCHANGE} = $icmd->{OUTSLOTS} - $icmd->{INSLOTS};
		}

		my $flags = '0';
		add_flag(\$flags, 'PEI') if $icmd->{MAYTHROW};
		add_flag(\$flags, $icmd->{CALLS}) if $icmd->{CALLS};

		$icmd->{FLAGS} = $flags;

		$maxflagslen = length($flags) if length($flags) > $maxflagslen;

		### calculate traits for building equivalence classes of ICMDs

		my $traits = 'TRAITS:';
		$traits .= ':DATAFLOW:'.$icmd->{DATAFLOW};
		$traits .= ':CONTROLFLOW:'.$icmd->{CONTROLFLOW};
		$traits .= ':ACTION:'.$icmd->{ACTION};
		$traits .= ':MAYTHROW:'.($icmd->{MAYTHROW} || '0');
		if ($icmd->{VERIFYCODE}) {
			$traits .= ':VERIFYCODE:'.join('',$icmd->{VERIFYCODE});
		}
		$icmd->{TRAITS} = $traits;
		push @{$icmdtraits{$traits}}, $icmd;

		my $vartraits = 'VARTRAITS:';
		$vartraits .= ':CONTROLFLOW:'.$icmd->{CONTROLFLOW};
		$vartraits .= ':MAYTHROW:'.($icmd->{MAYTHROW} || '0');
		if ($icmd->{VERIFYCODE}) {
			$vartraits .= ':VERIFYCODE:'.join('',$icmd->{VERIFYCODE});
		}
		if ($icmd->{DATAFLOW} =~ /^\d_TO_\d$/) {
			$vartraits .= ':OUTVARS:' . ($icmd->{OUTVARS} || '~');
			$vartraits .= ':BASICOUTTYPE:' . ($icmd->{BASICOUTTYPE} || '~');
		}
		else {
			$vartraits .= ':DATAFLOW:' . $icmd->{DATAFLOW};
			$vartraits .= ':ACTION:' . $icmd->{ACTION};
		}
		$icmd->{VARTRAITS} = $vartraits;
		push @{$icmdtraits{$vartraits}}, $icmd;
	}

	my $maxmax = 18;
	$maxactionlen = $maxmax if $maxactionlen > $maxmax;

	for my $icmdname (@icmds) {
		my $icmd = $icmds{$icmdname};

		$icmd->{FULLNAME_FILLED} = fill($icmd->{FULLNAME}, $maxfullnamelen);
		$icmd->{NAME_FILLED} = fill($icmd->{NAME}, $maxnamelen);
		$icmd->{ACTION_FILLED} = fill("(".$icmd->{ACTION}.")", $maxactionlen+2);
		$icmd->{FLAGS_FILLED} = fill($icmd->{FLAGS}, $maxflagslen);
	}
}

sub code
{
	my $text = join '', @_;

	my $newlines = () = $text =~ /\n/g;

	print $codefile $text;
	$codeline += $newlines;
}

sub write_verify_stackbased_stackchange
{
	my ($icmd) = @_;

	my $outslots = $icmd->{OUTSLOTS};
	my $inslots = $icmd->{INSLOTS};
	my $outtype = $icmd->{BASICOUTTYPE};
	my $stackchange = $icmd->{STACKCHANGE};

	my $modified = 0;

	if (defined($inslots) && defined($outslots)) {

		### modify stack pointer and write destination type

		if ($stackchange !=  0) {
			code "\tstack += ", $stackchange, ";\n";
		}

		if (defined($icmd->{VARIANTS}) && scalar @{$icmd->{VARIANTS}} == 1) {
			my $var = $icmd->{VARIANTS}->[0];

			if (defined($outtype)) {
				if ($outslots && ($inslots < $outslots || $var->{IN}->[0] ne $outtype)) {
					if ($outslots == 1) {
						code "\tstack[0].type = ", $cacaotypes{$outtype}, ";\n";
						$modified = 1;
					}
					elsif ($outslots == 2) {
						code "\tstack[0].type = TYPE_VOID;\n";
						code "\tstack[-1].type = ", $cacaotypes{$outtype}, ";\n";
						$modified = 1;
					}
				}
			}
		}
	}

	return $modified;
}

sub write_icmd_cases
{
	my ($icmd, $traits, $condition, $done) = @_;

	code "case ", $icmd->{FULLNAME}, ":\n";

	my $eqgroup = $icmdtraits{$icmd->{$traits}};
	my @actions = ($icmd->{ACTION});

	for my $ocmd (@$eqgroup) {
		next unless $condition->($ocmd);
		if ($ocmd->{FULLNAME} ne $icmd->{FULLNAME}) {
			code "case ", $ocmd->{FULLNAME}, ":\n";
			$done->{$ocmd->{FULLNAME}}++;

			unless (grep { $_ eq $ocmd->{ACTION} } @actions) {
				push @actions, $ocmd->{ACTION};
			}
		}
	}

	code "\t/* ", join(", ", map { "($_)" } @actions), " */\n";
}

sub write_icmd_set_props
{
	my ($icmd) = @_;

	if ($icmd->{MAYTHROW}) {
		code "\tmaythrow = true;\n";
	}
	if ($superblockend{$icmd->{CONTROLFLOW}}) {
		code "\tsuperblockend = true;\n";
	}
}

sub write_verify_stackbased_code
{
	my ($file) = @_;

	my %done;

	$codefile = $file;
	$codeline = 1;
	my $codefilename = $TYPECHECK_STACKBASED_INC;

	my $condition = sub { $_[0]->{STAGE} ne '--' and $_[0]->{STAGE} ne 'S+' };

	for my $icmdname (@icmds) {
		my $icmd = $icmds{$icmdname};

		next if $done{$icmdname};
		next unless $condition->($icmd);

		$done{$icmdname}++;

		my $outslots = $icmd->{OUTSLOTS};
		my $inslots = $icmd->{INSLOTS};
		my $outtype = $icmd->{BASICOUTTYPE};
		my $stackchange = $icmd->{STACKCHANGE};

		my @macros;

		### start instruction case, group instructions with same code

		code "\n";
		write_icmd_cases($icmd, 'TRAITS', $condition, \%done);

		### instruction properties

		write_icmd_set_props($icmd);

		### check stackdepth and stack types

		if (defined($inslots) && $inslots > 0) {
			code "\tCHECK_STACK_DEPTH($inslots);\n";
		}
		elsif (!defined($inslots)) {
			code "\t/* variable number of inslots! */\n";
		}

		if (defined($stackchange) && $stackchange > 0) {
			code "\tCHECK_STACK_SPACE(", $stackchange, ");\n";
		}
		elsif (!defined($outslots)) {
			code "\t/* variable number of outslots! */\n";
		}

		if (defined($inslots) && defined($outslots) && defined($icmd->{VARIANTS})
				&& scalar @{$icmd->{VARIANTS}} == 1)
		{
			my $var = $icmd->{VARIANTS}->[0];

			my $depth = 1 - $inslots;
			my $opindex = 1;
			for my $in (@{$var->{IN}}) {
				my $ctype = $cacaotypes{$in};
				my $slots = $slots{$in};
				if (defined($ctype)) {
					code "\tCHECK_STACK_TYPE(stack[$depth], $ctype);\n";
					$depth += $slots;
					$opindex++;
				}
			}
		}

		###	check local types

		if ($icmd->{DATAFLOW} eq 'LOAD') {
			code "\tCHECK_LOCAL_TYPE(IPTR->s1.varindex, ".$cacaotypes{$outtype}.");\n";
			if ($icmd->{VERIFYCODE}) {
				code "#\tdefine OP1 LOCAL_SLOT(IPTR->s1.varindex)\n";
				push @macros, 'OP1';
			}
		}
		elsif ($icmd->{DATAFLOW} eq 'IINC') {
			code "\tCHECK_LOCAL_TYPE(IPTR->s1.varindex, TYPE_INT);\n";
		}
		elsif ($icmd->{DATAFLOW} eq 'STORE') {
			my $intype = $icmd->{VARIANTS}->[0]->{IN}->[0];
			if ($slots{$intype} == 2) {
				code "\tSTORE_LOCAL_2_WORD(".$cacaotypes{$intype}.", IPTR->dst.varindex);\n";
			}
			else {
				code "\tSTORE_LOCAL(".$cacaotypes{$intype}.", IPTR->dst.varindex);\n";
			}
			if ($icmd->{VERIFYCODE}) {
				code "#\tdefine DST LOCAL_SLOT(IPTR->dst.varindex)\n";
				push @macros, 'DST';
			}
		}

		### custom verification code

		my $stackdone = 0;

		if ($icmd->{VERIFYCODE}) {
			if ($icmd->{VERIFYCODEPROPS}->{RESULTNOW}) {
				if (write_verify_stackbased_stackchange($icmd)) {
					code "\t/* CAUTION: stack types changed before custom code! */\n";
				}
				if ($stackchange) {
					code "\t/* CAUTION: stack pointer changed before custom code! */\n";
				}
				$stackdone = 1;
			}

			if (defined($inslots) && defined($outslots) && defined($icmd->{VARIANTS})
					&& scalar @{$icmd->{VARIANTS}} == 1)
			{
				my $var = $icmd->{VARIANTS}->[0];

				my $depth = 1 - $inslots;
				$depth -= $stackchange if $stackdone;
				my $opindex = 1;
				for my $in (@{$var->{IN}}) {
					my $ctype = $cacaotypes{$in};
					my $slots = $slots{$in};
					if (defined($ctype)) {
						code "#\tdefine OP$opindex (&(stack[$depth]))\n";
						push @macros, "OP$opindex";
						$depth += $slots;
						$opindex++;
					}
				}

				$depth = 1 - $inslots;
				$depth -= $stackchange if $stackdone;
				if ($outslots > 0) {
					code "#\tdefine DST  (&(stack[$depth]))\n";
					push @macros, "DST";
				}
			}

			if (defined($inslots) && defined($outslots)) {
				my $min = 1 - $inslots;
				my $max = $outslots - $inslots;
				$max = 0 if ($max < 0);
				if ($stackdone) {
					$min -= $stackchange;
					$max -= $stackchange;
				}
				if ($min <= $max) {
					code "\t/* may use stack[$min] ... stack[$max] */\n";
				}
			}

			code "\n";
			code "#\tline ".$icmd->{VERIFYCODELINE}." \"".$icmd->{VERIFYCODEFILE}."\"\n";
			code $_ for @{$icmd->{VERIFYCODE}};
			code "#\tline ", $codeline+1, " \"", $codefilename, "\"\n";
			code "\n";
		}

		### stack manipulation code

		if (!defined($icmd->{GOTOLABEL})) {

			unless ($stackdone) {
				write_verify_stackbased_stackchange($icmd);
			}

			code "\tbreak;\n";
		}
		else {
			code "\tgoto ", $icmd->{GOTOLABEL}, ";\n";
		}

		### undef macros that were defined above

		if (@macros) {
			code "\n";
			code "#\tundef $_\n" for @macros;
		}
		code "\n";
	}

	code "\n";

	code "/* vim:filetype=c:\n";
	code " */\n";
}

sub write_verify_variablesbased_code
{
	my ($file) = @_;

	my %done;

	$codefile = $file;
	$codeline = 1;
	my $codefilename = $TYPECHECK_VARIABLESBASED_INC;

	my $condition = sub { $_[0]->{STAGE} ne '--' and $_[0]->{STAGE} ne '-S' };

	for my $icmdname (@icmds) {
		my $icmd = $icmds{$icmdname};

		next if $done{$icmdname};
		next unless $condition->($icmd);

		$done{$icmdname}++;

		my $outvars = $icmd->{OUTVARS};
		my $invars = $icmd->{INVARS};
		my $outtype = $icmd->{BASICOUTTYPE};

		my @macros;

		### start instruction case, group instructions with same code

		code "\n";

		write_icmd_cases($icmd, 'VARTRAITS', $condition, \%done);

		### instruction properties

		write_icmd_set_props($icmd);

		###	check local types

		if ($icmd->{DATAFLOW} eq 'LOAD') {
			code "\tCHECK_LOCAL_TYPE(IPTR->s1.varindex, ".$cacaotypes{$outtype}.");\n";
			if ($icmd->{VERIFYCODE}) {
				code "#\tdefine OP1  VAROP(IPTR->s1)\n";
				push @macros, 'OP1';
			}
		}
		elsif ($icmd->{DATAFLOW} eq 'IINC') {
			code "\tCHECK_LOCAL_TYPE(IPTR->s1.varindex, TYPE_INT);\n";
		}
		elsif ($icmd->{DATAFLOW} eq 'STORE') {
			my $intype = $icmd->{VARIANTS}->[0]->{IN}->[0];
			if ($slots{$intype} == 2) {
				code "\tSTORE_LOCAL_2_WORD(".$cacaotypes{$intype}.", IPTR->dst.varindex);\n";
			}
			else {
				code "\tSTORE_LOCAL(".$cacaotypes{$intype}.", IPTR->dst.varindex);\n";
			}
			if ($icmd->{VERIFYCODE}) {
				code "#\tdefine DST  VAROP(IPTR->dst)\n";
				push @macros, 'DST';
			}
		}

		### custom verification code

		my $resultdone = 0;

		if ($icmd->{VERIFYCODE}) {
			if ($icmd->{VERIFYCODEPROPS}->{RESULTNOW}) {
				if (defined($outtype) && defined($outvars) && $outvars == 1) {
					code "\tVAROP(iptr->dst)->type = ", $cacaotypes{$outtype}, ";\n";
				}
				$resultdone = 1;
			}

			if (defined($invars) && $invars >= 1) {
				code "#\tdefine OP1  VAROP(iptr->s1)\n";
				push @macros, 'OP1';
			}

			if (defined($outvars) && $outvars == 1) {
				code "#\tdefine DST  VAROP(iptr->dst)\n";
				push @macros, 'DST';
			}

			code "\n";
			code "#\tline ".$icmd->{VERIFYCODELINE}." \"".$icmd->{VERIFYCODEFILE}."\"\n";
			code $_ for @{$icmd->{VERIFYCODE}};
			code "#\tline ", $codeline+1, " \"", $codefilename, "\"\n";
			code "\n";
		}

		### result code

		if (!defined($icmd->{GOTOLABEL})) {

			unless ($resultdone) {
				if (defined($outtype) && defined($outvars) && $outvars == 1) {
					code "\tVAROP(iptr->dst)->type = ", $cacaotypes{$outtype}, ";\n";
				}
			}

			code "\tbreak;\n";
		}
		else {
			code "\tgoto ", $icmd->{GOTOLABEL}, ";\n";
		}

		### undef macros that were defined above

		if (@macros) {
			code "\n";
			code "#\tundef $_\n" for @macros;
		}
		code "\n";
	}

	code "\n";

	code "/* vim:filetype=c:\n";
	code " */\n";
}

sub write_icmd_table
{
	my ($file) = @_;

	for my $icmdname (@icmds) {
		my $icmd = $icmds{$icmdname};

		printf $file "/*%3d*/ {", $icmd->{OPCODE};
		print $file 'N("', $icmd->{NAME_FILLED}, '") ';
		defined($icmd->{DATAFLOW}) or print STDERR "$0: warning: undefined data-flow: $icmdname\n";
		printf $file "DF_%-7s", $icmd->{DATAFLOW} || '0_TO_0';
		print $file ", ";
		printf $file "CF_%-6s", $icmd->{CONTROLFLOW} || 'NORMAL';
		print $file ", ";

		my $flags = $icmd->{FLAGS_FILLED};
		print $file $flags;

		print $file " /* ";

		my $stage = $icmd->{STAGE} || '  ';
		print $file $stage;
		print $file ' ';

		print $file "", $icmd->{ACTION_FILLED}, "";

		print $file " */},\n";
	}

	print $file "\n";
}

sub parse_icmd_table
{
	my ($filename) = (@_);
	my $file;

	$file = IO::File->new($filename) or die "$0: could not open file: $filename: $!\n";

	while (<$file>) {
		next if /^\s*$/;

		my $line = $_;

		# check if it looks like a table line

		next unless /^ \s* \/\* \s* (\d+) \s* \*\/ \s* (.*) $/x;
		my ($opc) = ($1);
		$_ = $2;

		# look at this monster! ;)

		if (/^      \{ \s* N\( \s* \" (\w+) \s* \" \)  # ICMD name --> $1
			   \s*  DF_(\w+) \s* ,                     # data-flow --> $2
			   \s*  CF_(\w+) \s* ,                     # control flow --> $3
			   \s*  ([^\/]*?)                          # the rest (flags) --> $4
			   \s*  \/\* \s* (\S+)?                    # stage --> $5
                         \s* \( ([^)]*) \)             # stack action --> $6
						 \s*
					\*\/
			   \s*  \} \s* ,?                          # closing brace and comma
			   \s*  (\/\* .* \*\/)?                    # optional comment
			   \s*  $/x)
		{
			my ($name, $df, $cf, $rest, $stage, $action) = ($1,$2,$3,$4,$5,$6);

			my $fn = 'ICMD_' . $name;
			push @icmds, $fn;

			my $icmd = {
				FULLNAME => $fn,
				NAME => $name,
				OPCODE => $opc,
				DATAFLOW => $df,
				CONTROLFLOW => $cf,
				ACTION => $action,
			};

			if ($stage) {
				$validstages{$stage} || die "$0: invalid stage: $filename:$.: $stage\n";
				$icmd->{STAGE} = $stage;
			}

			my @flags = split /\s*\|\s*/, $rest;

			for my $f (@flags) {
				$icmd->{MAYTHROW} = 1 if $f eq 'PEI';
				$icmd->{CALLS} = $f if $f =~ /^(.*_)?CALLS$/;
			}

			$icmds{$fn} = $icmd;
		}
		else {
			die "$0: invalid ICMD table line: $filename:$.: $line";
		}
	}

	close $file;
}

#################### main program

parse_icmd_table($opt_icmdtable);

if ($opt_stack) {
	parse_verify_code($VERIFY_C, 'STACK');
	post_process_icmds();

	my $outfile = IO::File->new(">$TYPECHECK_STACKBASED_INC")
			or die "$0: could not create: $TYPECHECK_STACKBASED_INC";
	write_verify_stackbased_code($outfile);
	close $outfile;
}
elsif ($opt_variables) {
	parse_verify_code($VERIFY_C, 'VARIABLES');
	post_process_icmds();

	my $outfile = IO::File->new(">$TYPECHECK_VARIABLESBASED_INC")
			or die "$0: could not create: $TYPECHECK_VARIABLESBASED_INC";
	write_verify_variablesbased_code($outfile);
	close $outfile;
}
elsif ($opt_table) {
	post_process_icmds();
	write_icmd_table(\*STDOUT);
}

