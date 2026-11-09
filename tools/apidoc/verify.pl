#!/usr/bin/perl
# ============================================================================
#  Check docs/api/api-data.js against the headers it was generated from.
#
#  The reference publishes every signature as the DECLARATION AS WRITTEN in
#  its header. That stays true only for as long as nobody edits a header, so
#  this says what has drifted:
#
#    GONE      a member that is no longer declared anywhere in its header -
#              the page documents something that does not exist
#    CHANGED   the declaration still exists but reads differently
#    MOVED     same declaration, different line
#    MOJIBAKE  prose that was encoded twice, so the page renders rubbish
#
#  Exit code is 1 if anything is GONE, CHANGED or MOJIBAKE, so it can gate a
#  commit. A MOVED member is only a stale line number and does not fail.
#
#    perl tools/apidoc/verify.pl
#    perl tools/apidoc/verify.pl --quiet      only the summary
# ============================================================================
use strict;
use warnings;
use JSON::PP;
use File::Basename qw(dirname);

my $ROOT = dirname(dirname(dirname(__FILE__)));
$ROOT = '.' unless -d "$ROOT/engine/include";
my $INC  = "$ROOT/engine/include";
my $DATA = "$ROOT/docs/api/api-data.js";

my $quiet = grep { $_ eq '--quiet' } @ARGV;

open(my $f, '<:raw', $DATA) or die "cannot read $DATA: $!\n";
local $/;
my $raw = <$f>;
close $f;
$raw =~ s/^.*?window\.ENGINE_API\s*=\s*//s;
$raw =~ s/;\s*$//s;
my $api = JSON::PP->new->utf8(1)->decode($raw);

# ------------------------------------------------- the bytes, not the shapes
#
#  Every signature on a page can match its header perfectly while the PROSE
#  around it is mangled, so this is a separate question from the three above
#  and just as fatal: the page is wrong on the screen.
#
#  How it goes wrong: this file is UTF-8, and a script that writes it with
#  JSON::PP's utf8(1) but no `use utf8` has already read its own em dashes as
#  three Latin-1 characters, so encode() encodes them a SECOND time. The page
#  then shows the three bytes of an em dash as three accented letters.
#
#  The test is not a list of known-bad sequences. After a correct decode, a
#  doubly-encoded character is a run of U+0080..U+00FF whose bytes are
#  THEMSELVES well-formed multi-byte UTF-8 - which no ordinary prose is.
my @mojibake;
{
    my $look;
    $look = sub {
        my ($node, $path) = @_;
        my $r = ref $node;
        if ($r eq 'HASH') {
            $look->($node->{$_}, "$path.$_") for sort keys %$node;
            return;
        }
        if ($r eq 'ARRAY') {
            for my $i (0 .. $#$node) {
                my $label = $i;
                $label = "$i:$node->[$i]{name}"
                    if ref $node->[$i] eq 'HASH' && defined $node->[$i]{name};
                $look->($node->[$i], "$path\[$label]");
            }
            return;
        }
        return if $r || !defined $node;
        while ($node =~ /([\x{80}-\x{FF}]{2,})/g) {
            my $run   = $1;
            my $bytes = pack 'C*', map { ord } split //, $run;
            next unless $bytes =~ /^(?: [\xC2-\xDF][\x80-\xBF]
                                      | \xE0[\xA0-\xBF][\x80-\xBF]
                                      | [\xE1-\xEF][\x80-\xBF]{2}
                                      | \xF0[\x90-\xBF][\x80-\xBF]{2}
                                      | [\xF1-\xF4][\x80-\xBF]{3} )+$/x;
            push @mojibake, sprintf '%s  %s  should be  %s', $path,
                 join(' ', map { sprintf 'U+%04X', ord } split //, $run), $bytes;
        }
    };
    $look->($api, '$');
}

# ---------------------------------------------------------------- the headers

my %FILE;
sub header_lines {
    my $h = shift;
    return $FILE{$h} if $FILE{$h};
    my $p = "$INC/$h";
    unless (-f $p) { return $FILE{$h} = [] }
    open(my $fh, '<:raw', $p) or return $FILE{$h} = [];
    local $/;
    my $t = <$fh>;
    close $fh;
    $t =~ s/\r\n/\n/g;
    return $FILE{$h} = [ split /\n/, $t, -1 ];
}

sub strip_comments {
    my $s = shift;
    $s =~ s{/\*.*?\*/}{ }gs;
    $s =~ s{//.*$}{}mg;
    return $s;
}

sub is_code {
    my $l = shift // '';
    return 0 if $l =~ m{^\s*(//|\*|/\*)};
    return 0 if $l =~ /^\s*$/;
    return 1;
}

# Join the declaration starting at 0-based index $i onto one line. Tracks paren
# depth, so a wrapped argument list is not cut at its first comma.
sub read_decl {
    my ($lines, $i) = @_;
    return '' if $i < 0 || $i > $#$lines;

    my $acc = '';
    my $limit = $i + 11;
    $limit = $#$lines if $limit > $#$lines;

    for (my $k = $i; $k <= $limit; $k++) {
        my $rawline = $lines->[$k];
        $acc .= ($acc eq '' ? '' : "\n") . $rawline;
        next if $rawline =~ /\\\s*$/;

        my $clean = strip_comments($acc);
        my $depth = 0;
        for my $ch (split //, $clean) { $depth++ if $ch eq '('; $depth-- if $ch eq ')' }
        next if $depth > 0;

        last if $clean =~ /[;{]/;
        last if $clean =~ /,\s*$/;
    }

    my $s = strip_comments($acc);
    $s =~ s/\s+/ /g;
    $s =~ s/^\s+|\s+$//g;

    if ($s !~ /^\s*(?:class|struct|enum|union|namespace)\b/) {
        my $depth = 0;
        my @ch = split //, $s;
        for my $i2 (0 .. $#ch) {
            my $c = $ch[$i2];
            $depth++ if $c eq '(';
            $depth-- if $c eq ')';
            next unless $c eq '{' && $depth <= 0;
            my $j = $i2 - 1;
            $j-- while $j >= 0 && $ch[$j] =~ /\s/;
            my $prev = $j >= 0 ? $ch[$j] : '';
            next if $prev =~ /[=,(>]/;
            my $head = substr($s, 0, $i2);
            $head =~ s/\s+$//;
            $s = $head if length $head > 3;
            last;
        }
    }

    if ($s =~ /^\s*#\s*define\s+(\w+)(\([^)]*\))?/) { $s = '#define ' . $1 . ($2 // '') }

    $s =~ s/\)\s*:\s*[A-Za-z_]\w*\s*\(.*$/)/;
    $s =~ s/\s*;\s*$/;/;
    $s =~ s/\s*,\s*$/,/;
    $s =~ s/\(\s+/(/g;
    $s =~ s/\s+\)/)/g;
    $s =~ s/\s+,/,/g;
    $s =~ s/<\s+/</g;
    $s =~ s/\s+>/>/g;
    return $s;
}

sub name_candidates {
    my $n = shift // '';
    $n =~ s/\s*\(.*\)\s*$//;
    $n =~ s/^.*:://;
    $n =~ s/^\s+|\s+$//g;
    return () unless length $n;
    my @c = ($n);
    if ($n =~ /^(?:Set|Is|Get|Has|Are)(.+)$/) { push @c, $1 }
    else { push @c, "Set$n", "Is$n", "Get$n", "Has$n" }
    push @c, 'operator' if $n =~ /operator/;
    return @c;
}

# A property PAIR is listed as one member with its two declarations joined by
# " | ". But " | " is also C++ bitwise-or, and "||" is in every hook concept,
# so a blind split tears `kStraights = kNorth | kEast | kSouth | kWest;` into
# four. Split only when every part reads like a declaration in its own right.
sub split_signature {
    my $s = shift // '';
    return ('') unless $s =~ /\S/;
    return ($s) if $s =~ /\|\|/;
    my @p = split / \| /, $s;
    return ($s) if @p < 2;
    for my $part (@p) {
        return ($s) unless $part =~ /\(/ || $part =~ /[;{]\s*$/;
    }
    return @p;
}

sub sig_ident {
    my $s = shift // '';
    $s =~ s{//.*$}{};
    return 'operator' if $s =~ /\boperator\b/;
    return $1 if $s =~ /([~A-Za-z_]\w*)\s*\(/;
    return $1 if $s =~ /([A-Za-z_]\w*)\s*(?:=|;|,|$)/;
    return '';
}

# Is this extracted text actually a declaration? Balanced parens, and it does
# not trail off mid-macro.
sub plausible {
    my $s = shift // '';
    return 0 unless $s =~ /\S/;
    my $o = ($s =~ tr/(//);
    my $c = ($s =~ tr/)//);
    return 0 if $o != $c;
    return 0 if $s =~ /\\\s*$/;
    return 1;
}

sub find_decl {
    my ($lines, $name, $claimed, $taken, $like) = @_;
    return -1 unless @$lines;
    $taken ||= {};

    my $rx = $name =~ /^operator/
        ? qr/\boperator\b/
        : do { my $q = quotemeta $name; qr/(?<![\w])$q(?![\w])/ };

    my @hits;
    for my $k (0 .. $#$lines) {
        next unless is_code($lines->[$k]);
        next if $taken->{$k};
        push @hits, $k if $lines->[$k] =~ $rx;
    }
    return -1 unless @hits;

    my $want = defined $claimed ? $claimed - 1 : $hits[0];
    my @decl = grep {
        my $l = $lines->[$_];
        $l =~ /[;({]/ || $l =~ /=/ || $l =~ /^\s*[A-Za-z_]\w*\s*(?:,|$)/
    } @hits;
    @decl = @hits unless @decl;

    # resolving one part of a joined group: prefer a candidate of the same
    # shape, or four deleted copy/move declarations take whichever same-named
    # line is nearest and one of them lands on `= default`
    if (defined $like && $like =~ /\S/) {
        for my $probe (qr/=\s*delete/, qr/=\s*default/, qr/&&/, qr/\bconst\b/, qr/\boperator\b/) {
            next unless $like =~ $probe;
            my @narrow = grep {
                my $two = $lines->[$_] . ' ' . ($_ < $#$lines ? $lines->[ $_ + 1 ] : '');
                $two =~ $probe
            } @decl;
            @decl = @narrow if @narrow;
        }
    }

    my ($best, $bestd);
    for my $k (@decl) {
        my $d = abs($k - $want);
        if (!defined $bestd || $d < $bestd) { $bestd = $d; $best = $k }
    }
    return $best;
}

# ------------------------------------------------------------------- the pass

my (@gone, @changed, @moved);
my ($types, $members, $missingHeaders) = (0, 0, 0);
my %seenHeader;

for my $t (@{ $api->{types} || [] }) {
    $types++;
    my $lines = header_lines($t->{header});
    if (!@$lines) {
        $missingHeaders++ unless $seenHeader{ $t->{header} }++;
        next;
    }

    for my $m (@{ $t->{members} || [] }) {
        # A lifecycle message has no declaration to check: ScriptBehaviour
        # deliberately declares none of them, which is the whole mechanism.
        next if ($m->{kind} // '') eq 'message';
        $members++;
        my @parts = split_signature($m->{signature} // '');

        my (@real, %taken, $firstLine);
        for my $pi (0 .. $#parts) {
            my @names = $pi == 0 ? name_candidates($m->{name}) : ();
            push @names, sig_ident($parts[$pi]);
            push @names, name_candidates($m->{name}) if $pi > 0;

            my $i = -1;
            for my $n (@names) {
                next unless defined $n && length $n;
                $i = find_decl($lines, $n, $m->{line}, \%taken, $parts[$pi]);
                last if $i >= 0;
            }
            next if $i < 0;
            $taken{$i} = 1;
            my $start = $i;
            $start-- if $start > 0 && $lines->[ $start - 1 ] =~ /^\s*template\s*</;
            my $decl = read_decl($lines, $start);
            next unless $decl =~ /\S/;
            next if $pi > 0 && !plausible($decl);
            push @real, $decl unless grep { $_ eq $decl } @real;
            $firstLine = $i + 1 unless defined $firstLine;
        }

        if (!@real) {
            push @gone, "$t->{name}.$m->{name}  ($t->{header})";
            next;
        }

        my $joined = join(' | ', @real);
        my ($a, $b) = ($joined, $m->{signature} // '');
        s/\s+//g for ($a, $b);
        if ($a ne $b) {
            push @changed, "$t->{name}.$m->{name}  ($t->{header}:$m->{line})\n"
                . "      documented: $m->{signature}\n"
                . "      header now: $joined";
        } elsif (defined $firstLine && $firstLine != ($m->{line} // 0)) {
            push @moved, "$t->{name}.$m->{name}  $t->{header}:$m->{line} -> $firstLine";
        }
    }
}

printf "checked %d types, %d members against %s\n", $types, $members, $INC;
printf "  headers not found: %d\n", $missingHeaders if $missingHeaders;

if (!$quiet) {
    if (@gone) {
        printf "\nGONE - documented but no longer declared (%d)\n", scalar @gone;
        print "  $_\n" for @gone;
    }
    if (@changed) {
        printf "\nCHANGED - the declaration reads differently now (%d)\n", scalar @changed;
        print "  $_\n" for @changed;
    }
    if (@mojibake) {
        printf "\nMOJIBAKE - text encoded twice, so the page shows rubbish (%d)\n",
               scalar @mojibake;
        print "  $_\n" for @mojibake[ 0 .. ($#mojibake > 29 ? 29 : $#mojibake) ];
        print "  ... and " . (scalar(@mojibake) - 30) . " more\n" if @mojibake > 30;
    }
    if (@moved) {
        printf "\nMOVED - same declaration, different line (%d)\n", scalar @moved;
        print "  $_\n" for @moved[ 0 .. ($#moved > 29 ? 29 : $#moved) ];
        print "  ... and " . (scalar(@moved) - 30) . " more\n" if @moved > 30;
    }
}

printf "\n%d gone, %d changed, %d moved, %d mojibake\n",
       scalar @gone, scalar @changed, scalar @moved, scalar @mojibake;
if (@mojibake && !@gone && !@changed) {
    print "The signatures match the headers, but some prose was encoded twice.\n";
    print "A script that writes api-data.js with JSON::PP utf8(1) needs `use utf8`\n";
    print "if its own source contains anything but ASCII.\n";
    exit 1;
}
if (@gone || @changed || @mojibake) {
    print "The reference is out of date with the headers. Regenerate it, or fix the headers.\n";
    exit 1;
}
print "The reference matches the headers.\n";
exit 0;
