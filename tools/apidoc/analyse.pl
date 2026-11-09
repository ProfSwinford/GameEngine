#!/usr/bin/perl
# ============================================================================
#  Read the compiler log from compile-examples.ps1 and say which examples are
#  actually wrong.
#
#  THE CLASSIFICATION IS PER TRANSLATION UNIT, not per line. A fragment that
#  documents one member starts in the middle of a function and refers to
#  things it never declared - a `collider`, an `m_alpha` the comment tells you
#  to add. MSVC reports the undeclared identifier and then CASCADES: the next
#  call "does not take 2 arguments" because one argument had no type, a
#  std::format call stops being a constant expression, a reference "must be
#  initialized". None of those are real, and counting them would bury the one
#  or two diagnostics that are.
#
#  So: a unit with ANY undeclared-identifier error is untrustworthy throughout
#  and counts as assuming context. A unit that fails WITHOUT one is a defect.
#
#  The complete examples are whole script files with nothing assumed, so they
#  are held to zero errors and reported separately. Exit code is 1 if any of
#  them fails.
#
#      perl tools/apidoc/analyse.pl [exampleDir]
# ============================================================================
use strict;
use warnings;
use File::Basename qw(dirname);

my $ROOT = dirname(dirname(dirname(__FILE__)));
$ROOT = '.' unless -d "$ROOT/engine/include";

my $EX  = $ARGV[0] // "$ROOT/build/apidoc-examples";
my $log = "$EX/compile.log";

my (%label, @all, %whole);
open(my $ix, '<', "$EX/index.tsv") or die "no index at $EX/index.tsv - run extract.pl first\n";
while (<$ix>) {
    chomp;
    my ($f, $l, $kind) = split /\t/, $_, 3;
    next unless $f;
    $label{$f} = $l // $f;
    $whole{$f} = 1 if ($kind // '') eq 'whole';
    push @all, $f;
}
close $ix;

open(my $fh, '<', $log) or die "no log at $log - run compile-examples.ps1 first\n";
my (%err, $total);
while (my $line = <$fh>) {
    next unless $line =~ /\b(?:error|fatal error)\s+(C\d+):\s*(.*)$/;
    my ($code, $msg) = ($1, $2);
    $msg =~ s/\s*\(compiling source file.*$//;
    my ($file) = $line =~ m{([A-Za-z0-9_]+\.cpp)\(\d+\)};
    next unless defined $file;
    my ($ln) = $line =~ m{\.cpp\((\d+)\)};
    push @{ $err{$file} }, { code => $code, msg => $msg, line => $ln // 0 };
    $total++;
}
close $fh;

sub is_undeclared {
    my $d = shift;
    return 1 if $d->{code} eq 'C2065' && $d->{msg} =~ /undeclared identifier/;
    return 1 if $d->{code} eq 'C3861' && $d->{msg} =~ /identifier not found/;
    return 1 if $d->{code} eq 'C2039' && $d->{msg} =~ /is not a member of/;
    # a receiver whose name happens to be a namespace - `fields`, `hooks` -
    # which a real parameter or local shadows, as the complete examples show
    return 1 if $d->{code} eq 'C2882';
    return 0;
}

my (@context, @defect, @wholeBad);
for my $f (sort keys %err) {
    my @ds = @{ $err{$f} };
    if (grep { is_undeclared($_) } @ds) {
        push @context, $f;
        push @wholeBad, [ $f, \@ds ] if $whole{$f};
        next;
    }
    push @defect, [ $f, \@ds ];
    push @wholeBad, [ $f, \@ds ] if $whole{$f};
}

sub report {
    my ($f, $ds) = @_;
    printf "\n  %s\n    (%s)\n", $label{$f} // $f, $f;
    my %seen;
    for my $e (@$ds) {
        my $k = "$e->{code} $e->{msg}";
        next if $seen{$k}++;
        printf "      line %-4d %s: %s\n", $e->{line}, $e->{code}, $e->{msg};
    }
}

my $nwhole = scalar grep { $whole{$_} } @all;
printf "%d translation units compiled (%d complete examples, %d wrapped fragments)\n",
    scalar @all, $nwhole, scalar(@all) - $nwhole;
printf "%d diagnostics across %d units\n\n", $total, scalar keys %err;

printf "COMPLETE EXAMPLES (held to zero errors): %d of %d clean\n",
    $nwhole - scalar @wholeBad, $nwhole;
report(@$_) for @wholeBad;

printf "\nFRAGMENTS THAT ASSUME A RECEIVER (expected, not defects): %d\n",
    scalar(grep { !$whole{$_} } @context);

my @fragDefect = grep { !$whole{ $_->[0] } } @defect;
printf "\nFRAGMENTS FAILING FOR SOME OTHER REASON: %d\n", scalar @fragDefect;
report(@$_) for @fragDefect;

exit(@wholeBad ? 1 : 0);
