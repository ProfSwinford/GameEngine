#!/usr/bin/perl
# ============================================================================
#  Build docs/api/api-data.js from the workflow harvest.
#
#  Reads the workflow journal (which records every agent return value), joins
#  the harvest groups to their verify groups by agent label, applies the
#  defects the verifiers raised, and then does the thing that actually makes
#  this reference trustworthy:
#
#    EVERY SIGNATURE IS RE-READ OUT OF THE HEADER. The agent's claimed
#    signature is used only to find the declaration; the text that gets
#    published is the file's own, joined onto one line. A member whose name
#    is nowhere in its header is dropped. A line number that is wrong is
#    corrected to the real one.
#
#  Run:  perl gen.pl <journalDir> [...more journalDirs]
# ============================================================================
use strict;
use warnings;
use JSON::PP;
use File::Basename qw(dirname);

my $ScriptDir = dirname(__FILE__);
my $REPO = dirname(dirname($ScriptDir));
$REPO = '.' unless -d "$REPO/engine/include";
my $INC  = "$REPO/engine/include";
my $OUT  = "$REPO/docs/api/api-data.js";

my @dirs = @ARGV;
die "usage: gen.pl <workflow transcript dir> [...]\n" unless @dirs;

# ---------------------------------------------------------------- the journal

my (%labelOf, %resultOf);
for my $dir (@dirs) {
    my $jf = "$dir/journal.jsonl";
    open(my $fh, '<:raw', $jf) or die "cannot read $jf: $!";
    while (my $line = <$fh>) {
        my $j = eval { decode_json($line) };
        next unless $j && ref $j eq 'HASH' && $j->{agentId};
        $labelOf{ $j->{agentId} } = $j->{label} if $j->{label};
        $resultOf{ $j->{agentId} } = $j->{result} if $j->{type} && $j->{type} eq 'result';
    }
    close $fh;
}

my (%harvest, %verify);
for my $id (keys %resultOf) {
    my $label = $labelOf{$id} or next;
    my $res = $resultOf{$id};
    next unless ref $res eq 'HASH';
    if ($label =~ /^harvest:(.+)$/) { $harvest{$1} = $res }
    elsif ($label =~ /^verify:(.+)$/) { $verify{$1} = $res }
}

printf "journal: %d harvest groups, %d verify groups\n", scalar keys %harvest, scalar keys %verify;

# -------------------------------------------------------------- header cache

my %FILE;
sub header_lines {
    my $h = shift;
    $h =~ s{^engine/include/}{};
    $h =~ s{^/+}{};
    return $FILE{$h} if $FILE{$h};
    my $p = "$INC/$h";
    unless (-f $p) { $FILE{$h} = []; return $FILE{$h} }
    open(my $f, '<:raw', $p) or do { $FILE{$h} = []; return $FILE{$h} };
    local $/;
    my $t = <$f>;
    close $f;
    $t =~ s/\r\n/\n/g;
    $FILE{$h} = [ split /\n/, $t, -1 ];
    return $FILE{$h};
}

sub norm_header {
    my $h = shift // '';
    $h =~ s{\\}{/}g;
    $h =~ s{^.*?engine/include/}{};
    $h =~ s{^/+}{};
    $h = "engine/$h" unless $h =~ m{^engine/};
    return $h;
}

# -------------------------------------------------- pull a declaration out of
#                                                    the header, authoritatively

sub strip_comments {
    my $s = shift;
    $s =~ s{/\*.*?\*/}{ }gs;
    $s =~ s{//.*$}{}mg;
    return $s;
}

# Does this line look like the start of a declaration rather than commentary?
sub is_code {
    my $l = shift // '';
    return 0 if $l =~ m{^\s*(//|\*|/\*)};
    return 0 if $l =~ /^\s*$/;
    return 1;
}

# Join the declaration starting at 0-based index $i into one line.
#
# The stop condition has to track PAREN DEPTH. A declaration whose argument
# list wraps - which is most of Gizmos - ends its first physical line on a
# comma, and a naive "stop at a comma" rule publishes half a signature.
sub read_decl {
    my ($lines, $i) = @_;
    return '' if $i < 0 || $i > $#$lines;

    my $acc = '';
    my $limit = $i + 11;
    $limit = $#$lines if $limit > $#$lines;

    for (my $k = $i; $k <= $limit; $k++) {
        my $raw = $lines->[$k];
        $acc .= ($acc eq '' ? '' : "\n") . $raw;
        next if $raw =~ /\\\s*$/;            # a macro continues

        my $clean = strip_comments($acc);
        my $depth = 0;
        for my $ch (split //, $clean) {
            $depth++ if $ch eq '(';
            $depth-- if $ch eq ')';
        }
        next if $depth > 0;                  # still inside an argument list

        last if $clean =~ /[;{]/;
        last if $clean =~ /,\s*$/;           # an enum value or a field
    }

    my $s = strip_comments($acc);
    $s =~ s/\s+/ /g;
    $s =~ s/^\s+|\s+$//g;

    # Drop a function body, but NOT a brace-initialised default argument.
    #
    # `const QueryFilter& filter = {}` is the common case and the naive "cut at
    # the first {" rule published half of every Physics2D query signature. A
    # real body opens at paren depth zero and is not preceded by = , ( or >.
    if ($s !~ /^\s*(?:class|struct|enum|union|namespace)\b/) {
        my $depth = 0;
        my @ch = split //, $s;
        for my $i (0 .. $#ch) {
            my $c = $ch[$i];
            $depth++ if $c eq '(';
            $depth-- if $c eq ')';
            next unless $c eq '{' && $depth <= 0;
            my $j = $i - 1;
            $j-- while $j >= 0 && $ch[$j] =~ /\s/;
            my $prev = $j >= 0 ? $ch[$j] : '';
            next if $prev =~ /[=,(>]/;
            my $head = substr($s, 0, $i);
            $head =~ s/\s+$//;
            $s = $head if length $head > 3;
            last;
        }
    }

    # A macro's interface is its name and parameters; the expansion is not.
    if ($s =~ /^\s*#\s*define\s+(\w+)(\([^)]*\))?/) {
        $s = '#define ' . $1 . ($2 // '');
    }

    # a member-initialiser list is implementation, not interface
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

# The identifiers a member might actually be declared under.
#
# Three things get in the way of a plain name match. Agents disambiguate
# overloads in the DISPLAY name ("ToString(UISpace)", "Entity (copy)"), they
# write nested members as "SdlWindowDeleter::operator()", and a getter/setter
# PAIR is listed under one property name ("Enabled") that appears nowhere in
# the file - only SetEnabled and IsEnabled do.
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

# The member identifier inside one declaration, used as a last-resort needle.
# A property PAIR is listed as one member with its two declarations joined by
# " | ". So is a group of deleted copy/move declarations. But " | " is also
# C++ bitwise-or, and "||" is in every hook concept - so a blind split tears
# `kStraights = kNorth | kEast | kSouth | kWest;` into four and then resolves
# each fragment to some unrelated line. Split only when every part reads like
# a declaration in its own right.
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

# $taken keeps the lines already claimed by earlier parts of this member, so a
# group of four deleted copy/move declarations resolves to four DIFFERENT lines
# instead of collapsing onto the nearest one twice.
sub find_any {
    my ($lines, $claimed, $taken, $lo, $hi, $like, @names) = @_;
    for my $n (@names) {
        next unless defined $n && length $n;
        my ($i, $st) = find_decl($lines, $n, $claimed, $taken, $lo, $hi, $like);
        return ($i, $st) if $i >= 0;
    }
    return (-1, 'absent');
}

# Is this extracted text actually a declaration? Balanced parens, and it ends
# the way a declaration ends. Used to drop a part of a joined signature that
# resolved onto something that is not one - a concept's use inside a macro,
# say - rather than publish it.
sub plausible {
    my $s = shift // '';
    return 0 unless $s =~ /\S/;
    my $o = ($s =~ tr/(//);
    my $c = ($s =~ tr/)//);
    return 0 if $o != $c;
    return 0 if $s =~ /\\\s*$/;
    return 1;
}

# Where in the header is this member really declared? Returns 0-based index.
# The line span of a type's own body, by brace matching from its declaration.
# Restricting the search to it is what stops an enum value called `Default`
# from resolving to a `= GizmoCategory::Default` default argument three
# hundred lines further down.
sub type_range {
    my ($lines, $line, $kind) = @_;
    return (0, $#$lines) unless @$lines;
    return (0, $#$lines) if ($kind // '') =~ /^(namespace|functions|macro|alias)$/;
    my $i = ($line // 1) - 1;
    return (0, $#$lines) if $i < 0 || $i > $#$lines;

    my ($depth, $started) = (0, 0);
    for my $k ($i .. $#$lines) {
        my $l = strip_comments($lines->[$k]);
        for my $c (split //, $l) {
            if ($c eq '{') { $depth++; $started = 1 }
            elsif ($c eq '}') {
                $depth--;
                return ($i, $k) if $started && $depth <= 0;
            }
        }
        return ($i, $k) if $started && $depth <= 0;
        last if !$started && $k > $i + 4;    # no body found: a forward decl
    }
    return (0, $#$lines);
}

sub find_decl {
    my ($lines, $name, $claimed, $taken, $lo, $hi, $like) = @_;
    return (-1, 'absent') unless @$lines;
    $taken ||= {};
    $lo = 0        unless defined $lo;
    $hi = $#$lines unless defined $hi;

    my $needle = $name;
    my $rx;
    if ($needle =~ /^operator/) {
        $rx = qr/\boperator\b/;
    } else {
        my $q = quotemeta $needle;
        $rx = qr/(?<![\w])$q(?![\w])/;
    }

    my @hits;
    for my $k ($lo .. $hi) {
        next unless is_code($lines->[$k]);
        next if $taken->{$k};
        push @hits, $k if $lines->[$k] =~ $rx;
    }
    # fall back to the whole file: a member the agent attributed to one type
    # may be declared at namespace scope, and free functions always are
    if (!@hits && ($lo > 0 || $hi < $#$lines)) {
        for my $k (0 .. $#$lines) {
            next unless is_code($lines->[$k]);
            next if $taken->{$k};
            push @hits, $k if $lines->[$k] =~ $rx;
        }
    }
    return (-1, 'absent') unless @hits;

    # prefer a hit that looks like a declaration of this thing, nearest the
    # claimed line. A bare enum value - `Default,` on its own line - counts:
    # leaving it out is what let a default argument win instead.
    my $want = defined $claimed ? $claimed - 1 : $hits[0];
    my @decl = grep {
        my $l = $lines->[$_];
        $l =~ /[;({]/ || $l =~ /=/ || $l =~ /^\s*[A-Za-z_]\w*\s*(?:,|$)/
    } @hits;
    @decl = @hits unless @decl;

    # When the caller knows roughly what the declaration should look like -
    # resolving one part of a joined group - prefer a candidate of the same
    # shape. Without this, four deleted copy/move declarations take whichever
    # same-named line is nearest, and one of them lands on `= default`.
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
    my $status = $bestd <= 3 ? 'ok' : 'moved';
    return ($best, $status);
}

# ------------------------------------------------------- apply verify defects

my %DEFECT_COUNT;

sub apply_defects {
    my ($group, $api, $v) = @_;
    return unless $v && ref $v eq 'HASH' && ref $v->{defects} eq 'ARRAY';

    my %byName = map { $_->{name} => $_ } @{ $api->{types} || [] };

    for my $d (@{ $v->{defects} }) {
        my $kind = $d->{kind} // 'other';
        $DEFECT_COUNT{$kind}++;
        my $tn = $d->{typeName} // '';
        my $mn = $d->{memberName} // '';
        my $corr;
        if (defined $d->{correctionJson} && $d->{correctionJson} =~ /\S/) {
            $corr = eval { decode_json($d->{correctionJson}) };
            if (!$corr) {
                my $c = $d->{correctionJson};
                $c =~ s/^\s*```(?:json)?\s*//; $c =~ s/\s*```\s*$//;
                $corr = eval { decode_json($c) };
            }
        }

        if ($kind eq 'missing-type') {
            next unless $corr && ref $corr eq 'HASH' && $corr->{name};
            next if $byName{ $corr->{name} };
            $corr->{members} ||= [];
            push @{ $api->{types} }, $corr;
            $byName{ $corr->{name} } = $corr;
            next;
        }

        my $t = $byName{$tn} or next;

        if ($kind eq 'wrong-audience') {
            $t->{audience} = $corr->{audience} if $corr && $corr->{audience};
            next;
        }

        if ($kind eq 'fabricated-member') {
            $t->{members} = [ grep { ($_->{name} // '') ne $mn } @{ $t->{members} || [] } ];
            next;
        }

        if ($kind eq 'missing-member') {
            next unless $corr && ref $corr eq 'HASH' && $corr->{name};
            next if grep { ($_->{name} // '') eq $corr->{name} && ($_->{signature} // '') eq ($corr->{signature} // '') }
                    @{ $t->{members} || [] };
            push @{ $t->{members} }, $corr;
            next;
        }

        # wrong-signature / wrong-line / wrong-description: patch in place
        next unless $corr && ref $corr eq 'HASH';
        my ($m) = grep { ($_->{name} // '') eq $mn } @{ $t->{members} || [] };
        if (!$m) {
            push @{ $t->{members} }, $corr if $corr->{name};
            next;
        }
        for my $f (qw(signature line summary description returns unity kind access example params)) {
            $m->{$f} = $corr->{$f} if exists $corr->{$f} && defined $corr->{$f}
                && (!ref $corr->{$f} ? $corr->{$f} =~ /\S/ : 1);
        }
    }
}

for my $g (sort keys %harvest) {
    apply_defects($g, $harvest{$g}, $verify{$g});
}

# ------------------------------------------------------------ merge the types

my (%TYPE, @TYPE_ORDER);
my $dup = 0;
for my $g (sort keys %harvest) {
    for my $t (@{ $harvest{$g}{types} || [] }) {
        next unless $t->{name};
        $t->{header} = norm_header($t->{header});
        $t->{group} = $g;
        if (my $old = $TYPE{ $t->{name} }) {
            $dup++;
            # keep whichever carries more members
            my $a = scalar @{ $old->{members} || [] };
            my $b = scalar @{ $t->{members} || [] };
            next if $a >= $b;
            %$old = %$t;
            next;
        }
        $TYPE{ $t->{name} } = $t;
        push @TYPE_ORDER, $t->{name};
    }
}
printf "merged %d types (%d duplicate names resolved)\n", scalar @TYPE_ORDER, $dup;

# ------------------------------------------------------- repair what arrived
#
# A type a VERIFIER added as a missing-type may carry no header path, or carry
# the namespace glued onto its name. Both are recoverable: the header from
# what the rest of its group says, the name by dropping a prefix that is just
# its own namespace. A nested type's real `Outer::Inner` name is left alone.

my %groupHeader;
for my $n (@TYPE_ORDER) {
    my $t = $TYPE{$n};
    next unless $t->{header} && -f "$INC/$t->{header}";
    $groupHeader{ $t->{group} }{ $t->{header} }++;
}

my ($renamed, $rehomed, $homeless) = (0, 0, 0);
my @ordered = map { $TYPE{$_} } @TYPE_ORDER;

for my $t (@ordered) {
    my $ns = $t->{ns} // 'eng';
    if ($t->{name} =~ /^\Q$ns\E::(.+)$/ || $t->{name} =~ /^eng::(.+)$/) {
        $t->{name} = $1;
        $renamed++;
    }
    next if $t->{header} && -f "$INC/$t->{header}";
    my $byCount = $groupHeader{ $t->{group} } || {};

    # the header that actually DECLARES it, not merely the group's commonest -
    # the three SDL deleters live in SdlHandles.h while the platform group is
    # mostly EventPump.h, and guessing by frequency put them in the wrong file
    # and then dropped their one member as absent
    my $q = quotemeta $t->{name};
    my ($best) = grep {
        my $ls = header_lines($_);
        scalar grep { $_ =~ /(?:class|struct|enum|union)\s+$q\b/ } @$ls;
    } sort keys %$byCount;

    ($best) = sort { $byCount->{$b} <=> $byCount->{$a} || $a cmp $b } keys %$byCount
        unless $best;

    if ($best) { $t->{header} = $best; $rehomed++ }
    else { $homeless++ }
}

%TYPE = ();
@TYPE_ORDER = ();
my $collided = 0;
for my $t (@ordered) {
    if (!$t->{header} || !-f "$INC/$t->{header}") { next }
    if ($TYPE{ $t->{name} }) {
        my $old = $TYPE{ $t->{name} };
        $collided++;
        next if scalar @{ $old->{members} || [] } >= scalar @{ $t->{members} || [] };
        %$old = %$t;
        next;
    }
    $TYPE{ $t->{name} } = $t;
    push @TYPE_ORDER, $t->{name};
}
printf "  repaired: %d names unqualified, %d headers inferred from the group, %d dropped as unplaceable, %d late collisions\n",
    $renamed, $rehomed, $homeless, $collided
    if $renamed || $rehomed || $homeless || $collided;

# --------------------------------------------- verify against the real headers

my %STAT = (ok => 0, moved => 0, resigned => 0, dropped => 0, noline => 0);
my @DROPPED;

for my $name (@TYPE_ORDER) {
    my $t = $TYPE{$name};
    my $lines = header_lines($t->{header});

    # the type's own line
    if (@$lines) {
        my ($i, $st) = find_decl($lines, $t->{name}, $t->{line});
        if ($i >= 0) {
            $t->{line} = $i + 1;
        }
    }

    my ($lo, $hi) = type_range($lines, $t->{line}, $t->{kind});

    my @keep;
    for my $m (@{ $t->{members} || [] }) {
        next unless defined $m->{name} && $m->{name} =~ /\S/;
        $m->{kind} ||= 'method';
        $m->{summary} //= '';
        # a destructor belongs beside the constructors, however it was labelled
        $m->{kind} = 'constructor' if $m->{name} =~ /^~/;

        if (!@$lines) { $STAT{noline}++; push @keep, $m; next; }

        # A property pair is listed as one member with both declarations joined
        # by " | ". Resolve each half separately, so the published text is the
        # file's for both of them.
        my @parts = split_signature($m->{signature} // '');

        my (@real, $firstLine, $anyMoved, $allAbsent, %taken);
        $allAbsent = 1;
        for my $pi (0 .. $#parts) {
            my @names = $pi == 0 ? name_candidates($m->{name}) : ();
            push @names, sig_ident($parts[$pi]);
            push @names, name_candidates($m->{name}) if $pi > 0;

            my ($i, $st) = find_any($lines, $m->{line}, \%taken, $lo, $hi, $parts[$pi], @names);
            next if $i < 0;
            $taken{$i} = 1;
            $allAbsent = 0;

            my $start = $i;
            if ($start > 0 && $lines->[ $start - 1 ] =~ /^\s*template\s*</) { $start-- }
            my $decl = read_decl($lines, $start);
            next unless $decl =~ /\S/;
            # the first part must stand; a later one that is not a declaration
            # is dropped rather than published
            if ($pi > 0 && !plausible($decl)) { $STAT{implausible}++; next }
            $anyMoved = 1 if $st ne 'ok';
            push @real, $decl unless grep { $_ eq $decl } @real;
            $firstLine = $i + 1 unless defined $firstLine;
        }

        if ($allAbsent || !@real) {
            $STAT{dropped}++;
            push @DROPPED, "$t->{name}::$m->{name}  ($t->{header})";
            next;
        }

        my $joined = join(' | ', @real);
        my ($a, $b) = ($joined, $m->{signature} // '');
        s/\s+//g for ($a, $b);
        $STAT{resigned}++ if $a ne $b;
        $m->{signature} = $joined;
        $m->{line} = $firstLine if defined $firstLine;
        $STAT{ $anyMoved ? 'moved' : 'ok' }++;
        push @keep, $m;
    }
    $t->{members} = \@keep;
}

printf "signatures: %d at the claimed line, %d relocated, %d rewritten from the header, %d dropped as absent\n",
    $STAT{ok}, $STAT{moved}, $STAT{resigned}, $STAT{dropped};
if (@DROPPED) {
    print "  dropped:\n";
    print "    $_\n" for @DROPPED;
}

# ---------------------------------------------- corrections from the compiler
#
# Every example was compiled against the real headers. The handful that did not
# are corrected here rather than by hand-editing generated output, and an entry
# that matches nothing is reported - so this file cannot rot unnoticed.

my $FIXUPS = "$ScriptDir/fixups.json";
if (-f $FIXUPS) {
    open(my $fx, '<:raw', $FIXUPS) or die "read $FIXUPS: $!";
    local $/;
    my $raw = <$fx>;
    close $fx;
    my $fix = eval { JSON::PP->new->utf8(1)->decode($raw) };
    if (!$fix) {
        print "  WARNING: fixups.json did not parse; no corrections applied\n";
    } else {
        my $applied = 0;
        for my $s (@{ $fix->{sub} || [] }) {
            my $hits = 0;
            for my $name (@TYPE_ORDER) {
                my $t = $TYPE{$name};
                next if defined $s->{type} && $t->{name} ne $s->{type};
                for my $ex (@{ $t->{examples} || [] }) {
                    next unless defined $ex->{code};
                    my $n = ($ex->{code} =~ s/\Q$s->{find}\E/$s->{replace}/g);
                    $hits += $n;
                }
                for my $m (@{ $t->{members} }) {
                    next unless defined $m->{example};
                    my $n = ($m->{example} =~ s/\Q$s->{find}\E/$s->{replace}/g);
                    $hits += $n;
                }
            }
            if ($hits) { $applied += $hits }
            else {
                printf "  STALE FIXUP (matched nothing): %s :: %s\n",
                    ($s->{type} // 'any'), substr($s->{find}, 0, 60);
            }
        }
        printf "example corrections applied: %d\n", $applied;

        # THE LIFECYCLE CALLBACKS. ScriptBehaviour deliberately declares none
        # of them - that absence is the mechanism, because it is what lets the
        # compiler tell which ones a script actually wrote - so there is no
        # declaration for the extraction pass to find and they have to be
        # stated. Each carries the ScriptHooks.h line of the concept that
        # detects it, and is skipped by the header check below.
        my $msgs = 0;
        for my $typeName (sort keys %{ $fix->{messages} || {} }) {
            my $t = $TYPE{$typeName};
            if (!$t) { print "  STALE FIXUP: no type $typeName for its messages\n"; next }
            for my $m (@{ $fix->{messages}{$typeName} }) {
                next unless $m->{name};
                next if grep { ($_->{name} // '') eq $m->{name} } @{ $t->{members} };
                $m->{kind} = 'message';
                push @{ $t->{members} }, $m;
                $msgs++;
            }
        }
        printf "lifecycle messages added: %d\n", $msgs if $msgs;
    }
}

# ------------------------------------------------------------------- slugging

my %OPWORD = (
    '<=>' => 'cmp', '==' => 'eq', '!=' => 'ne', '<=' => 'le', '>=' => 'ge',
    '+=' => 'addeq', '-=' => 'subeq', '*=' => 'muleq', '/=' => 'diveq', '%=' => 'modeq',
    '++' => 'inc', '--' => 'dec', '->' => 'arrow', '[]' => 'index', '()' => 'call',
    '<<' => 'shl', '>>' => 'shr', '&&' => 'and', '||' => 'or',
    '+' => 'add', '-' => 'sub', '*' => 'mul', '/' => 'div', '%' => 'mod',
    '<' => 'lt', '>' => 'gt', '=' => 'assign', '!' => 'not', '&' => 'amp', '|' => 'pipe',
);

sub slugify {
    my $n = shift;
    if ($n =~ /^operator\s*(.*)$/) {
        my $sym = $1;
        $sym =~ s/\s+//g;
        my $word = $OPWORD{$sym};
        if (!$word) {
            $word = $sym;
            $word =~ s/[^A-Za-z0-9]//g;
            $word = 'op' unless length $word;
        }
        return "operator-$word";
    }
    my $s = $n;
    $s =~ s/[^A-Za-z0-9]+/-/g;
    $s =~ s/^-+|-+$//g;
    $s = 'member' unless length $s;
    return $s;
}

# A ROUTING SLUG PER TYPE, separate from its display name.
#
# Grouping entries are named things like "Vec2 operators" and
# "ScriptRegistry::Entry", which read correctly as a page title and are
# unusable in a URL fragment: the artifact platform passes only a plain
# `#name` of letters, digits and hyphens, and anything else arrives
# percent-encoded and resolves to nothing.
my %slugUsed;
for my $name (@TYPE_ORDER) {
    my $t = $TYPE{$name};
    my $s = $t->{name};
    $s =~ s/::/-/g;
    $s =~ s/[^A-Za-z0-9]+/-/g;
    $s =~ s/^-+|-+$//g;
    $s = 'type' unless length $s;
    if ($slugUsed{$s}) { my $k = ++$slugUsed{$s}; $t->{slug} = "$s-$k" }
    else { $slugUsed{$s} = 1; $t->{slug} = $s }
}

for my $name (@TYPE_ORDER) {
    my $t = $TYPE{$name};
    my %used;
    for my $m (@{ $t->{members} }) {
        my $s = slugify($m->{name});
        if ($used{$s}) { my $n = ++$used{$s}; $m->{slug} = "$s-$n" }
        else { $used{$s} = 1; $m->{slug} = $s }
    }
}

# -------------------------------------------------------------------- modules

my @MODULE_DEF = (
    {
        key => "scripting", title => "Scripting",
        tagline => "The base class, the hooks, and the Inspector fields.",
        headers => 'engine/scene/Script*.h, engine/tools/ScriptVars.h',
        blurb => "The base class a game script inherits, the five Unity-spelled handles on it, the hook\n"
               . "detection that lets you write only the callbacks you want, and the field system that turns a\n"
               . "public variable into an Inspector control.\n\nStart at `ScriptBehaviour`.",
    },
    {
        key => "debug", title => "Debugging",
        tagline => "Unity's Debug class: logging, shapes, and numbers on the screen.",
        headers => 'engine/debug/Debug.h',
        blurb => "One facade over the two things a bug hunt is made of: a line in the Console and a shape on top of\n"
               . "the world. It takes no channel, where the engine's own `ENGINE_LOG_*` macros demand one on every\n"
               . "call, and it adds the throttles - `LogOnce`, `LogEvery` - that stop a message from `OnUpdate`\n"
               . "arriving sixty times a second.\n\nStart at `Debug`.",
    },
    {
        key => "scene", title => "Scene and entities",
        tagline => "The world and the things in it.",
        headers => 'engine/scene/Scene.h, Entity.h, Component.h, Messaging.h, DeferredOps.h, SystemOrder.h',
        blurb => "The world and the things in it. A `Scene` holds entities; an `Entity` is a name, an id and a\n"
               . "bag of components. Creating, finding, parenting and destroying all go through these, and\n"
               . "destroying is queued rather than immediate.",
    },
    {
        key => "prefabs", title => "Prefabs",
        tagline => "Saved trees of entities, and their overrides.",
        headers => 'engine/scene/Prefab.h, PrefabRef.h',
        blurb => "A saved tree of entities, and the override model that lets one instance differ from the asset\n"
               . "it came from. `PrefabRef` is the script-facing end: an Inspector slot you spawn from.",
    },
    {
        key => "math", title => "Math and geometry",
        tagline => "Vectors, the transform, and the shape vocabulary.",
        headers => 'engine/math/*.h',
        blurb => "Vectors, the transform, and the shape vocabulary the physics queries speak. **One world unit\n"
               . "is one pixel** and rotations are in degrees.",
    },
    {
        key => "physics", title => "Physics",
        tagline => "Bodies, colliders, contacts, and the query API.",
        headers => 'engine/physics/*.h',
        blurb => "Bodies, colliders, contacts and the query API. The simulation runs at a fixed 1/60 s step with\n"
               . "gravity `(0, -981)`, so movement belongs in `OnFixedUpdate` and never in `OnUpdate`.",
    },
    {
        key => "render", title => "Rendering and cameras",
        tagline => "Colours, gizmos, cameras, textures and fonts.",
        headers => 'engine/render/*.h, engine/scene/CameraComponent.h, ImageRef.h',
        blurb => "What ends up on screen. Most of it runs itself from the sprite and tilemap components; the\n"
               . "parts a script reaches for are `Color`, `Gizmos` for debug drawing, and the camera.",
    },
    {
        key => "animation", title => "Sprite animation",
        tagline => "Flipbook frames out of a sheet.",
        headers => 'engine/scene/SpriteAnimation.h, AnimationRef.h',
        blurb => "Flipbook animation: a named set of frames out of a sheet, advanced by a system rather than by\n"
               . "your own counter. `AnimationRef` is the Inspector slot that names one.",
    },
    {
        key => "ui", title => "User interface",
        tagline => "Screen-space rects, text, images, buttons, sliders.",
        headers => 'engine/scene/UI.h',
        blurb => "Screen-space components: a rect that anchors, and text, images, buttons and sliders inside it.\n"
               . "Laid out and drawn by `UISystem` once per frame.",
    },
    {
        key => "input", title => "Input",
        tagline => "Named actions and the pointer.",
        headers => 'engine/input/InputMap.h, Pointer.h',
        blurb => "Named actions rather than raw key codes, and the pointer. **Which hook you read input in\n"
               . "matters**: an edge-triggered read belongs in `OnUpdate`, where it is sampled once per frame.",
    },
    {
        key => "tilemap", title => "Tilemaps",
        tagline => "A chunked grid, its tile assets, and the autotiler.",
        headers => 'engine/tilemap/*.h',
        blurb => "A chunked grid of tiles, the tile assets it refers to, the autotiler that picks artwork from a\n"
               . "cell's neighbours, and the collider that fuses runs of solid tiles into one shape.",
    },
    {
        key => "core", title => "Core and time",
        tagline => "The clock, the log, and the engine object.",
        headers => 'engine/Engine.h, engine/core/*.h',
        blurb => "The clock, the log, and the engine object itself. `Time` is the one a script reads constantly;\n"
               . "most of the rest is the host side, documented because the umbrella header makes it visible.",
    },
    {
        key => "assets", title => "Assets and files",
        tagline => "Loading, caching, importing, virtual paths.",
        headers => 'engine/resource/*.h, engine/fs/*.h, engine/core/Json.h',
        blurb => "Loading, caching and importing. A game addresses assets by virtual path; the import settings\n"
               . "are what the editor writes beside an image to say how it should be cut up.",
    },
    {
        key => "platform", title => "Platform",
        tagline => "The window and the OS event pump. Not for game code.",
        headers => 'engine/platform/*.h, engine/tools/GuiHooks.h',
        blurb => "The window, the OS event pump and the editor GUI hooks. **None of this is for game code** --\n"
               . "a script uses `InputMap` and `Pointer` instead.",
    },
);

my %H2M = (
    'engine/scene/ScriptComponent.h' => 'scripting',
    'engine/scene/ScriptHooks.h'     => 'scripting',
    'engine/scene/ScriptFields.h'    => 'scripting',
    'engine/scene/ScriptLibrary.h'   => 'scripting',
    'engine/tools/ScriptVars.h'      => 'scripting',

    'engine/scene/Scene.h'           => 'scene',
    'engine/scene/Entity.h'          => 'scene',
    'engine/scene/EntityId.h'        => 'scene',
    'engine/scene/EntityRef.h'       => 'scene',
    'engine/scene/Component.h'       => 'scene',
    'engine/scene/DeferredOps.h'     => 'scene',
    'engine/scene/Messaging.h'       => 'scene',
    'engine/scene/SystemOrder.h'     => 'scene',
    'engine/scene/SpinComponent.h'   => 'scene',
    'engine/scene/BuildScenes.h'     => 'scene',

    'engine/scene/Prefab.h'          => 'prefabs',
    'engine/scene/PrefabRef.h'       => 'prefabs',

    'engine/scene/CameraComponent.h' => 'render',
    'engine/scene/ImageRef.h'        => 'render',

    'engine/scene/SpriteAnimation.h' => 'animation',
    'engine/scene/AnimationRef.h'    => 'animation',

    'engine/scene/UI.h'              => 'ui',

    'engine/Engine.h'                => 'core',
    'engine/tools/GuiHooks.h'        => 'platform',
);

sub module_for {
    my $h = shift;
    return $H2M{$h} if $H2M{$h};
    return 'debug'    if $h =~ m{^engine/debug/};
    return 'math'     if $h =~ m{^engine/math/};
    return 'physics'  if $h =~ m{^engine/physics/};
    return 'render'   if $h =~ m{^engine/render/};
    return 'input'    if $h =~ m{^engine/input/};
    return 'tilemap'  if $h =~ m{^engine/tilemap/};
    return 'core'     if $h =~ m{^engine/core/};
    return 'assets'   if $h =~ m{^engine/(resource|fs)/};
    return 'platform' if $h =~ m{^engine/platform/};
    return 'scene'    if $h =~ m{^engine/scene/};
    return 'core';
}

my %AUDRANK = (script => 0, system => 1, host => 2);

my @MODULES;
my %assigned;
for my $def (@MODULE_DEF) {
    # ALPHABETICAL, flatly. An earlier version led with a curated handful and
    # then grouped by audience, which reads well once and is no use at all
    # when you are scanning the sidebar for a name you already know. The
    # coloured dot on each row still says who the type is for.
    my @names = sort { lc($a) cmp lc($b) }
                grep { module_for($TYPE{$_}{header}) eq $def->{key} } @TYPE_ORDER;
    $assigned{$_} = 1 for @names;
    push @MODULES, {
        key => $def->{key}, title => $def->{title}, tagline => $def->{tagline},
        blurb => $def->{blurb}, headers => $def->{headers},
        types => \@names,
    };
    printf "  %-10s %3d types\n", $def->{key}, scalar @names;
}
my @orphan = grep { !$assigned{$_} } @TYPE_ORDER;
print "  ORPHANS: @orphan\n" if @orphan;

# --------------------------------------------------------------------- guides

my @GUIDES = (
    { slug => 'start',     title => 'Your first script',        lede => 'One include, one base class, one macro.' },
    { slug => 'lifecycle', title => 'The lifecycle',            lede => 'Every hook, and which one a thing belongs in.' },
    { slug => 'inspector', title => 'Variables in the Inspector', lede => 'A public field is a control, with no code.' },
    { slug => 'unity',     title => 'Coming from Unity',        lede => 'What is spelled the same, and the three things that are not.' },
    { slug => 'gaps',      title => 'What is not here yet',     lede => 'The honest list, so nobody goes looking.' },
    { slug => 'reading',   title => 'How to read these pages',  lede => 'Where the signatures come from, and what the marks mean.' },
);

# ----------------------------------------------------------------- emit the js

my @out;
for my $name (@TYPE_ORDER) {
    my $t = $TYPE{$name};
    my %o = (
        name => $t->{name},
        slug => $t->{slug},
        kind => $t->{kind} // 'class',
        ns => $t->{ns} // 'eng',
        header => $t->{header},
        line => $t->{line} // 1,
        audience => ($AUDRANK{ $t->{audience} // '' } ? $t->{audience} : ($t->{audience} // 'system')),
        summary => $t->{summary} // '',
    );
    $o{audience} = 'system' unless exists $AUDRANK{ $o{audience} };
    for my $f (qw(inherits unity description remarks)) {
        $o{$f} = $t->{$f} if defined $t->{$f} && $t->{$f} =~ /\S/;
    }
    $o{members} = [ map {
        my $m = $_;
        my %mo = (
            name => $m->{name}, kind => $m->{kind}, slug => $m->{slug},
            signature => $m->{signature} // '', line => $m->{line} // 0,
            summary => $m->{summary} // '',
        );
        for my $f (qw(description returns unity example access header)) {
            $mo{$f} = $m->{$f} if defined $m->{$f} && !ref $m->{$f} && $m->{$f} =~ /\S/;
        }
        if (ref $m->{params} eq 'ARRAY' && @{ $m->{params} }) {
            $mo{params} = [ map { { name => ($_->{name} // ''), type => ($_->{type} // ''), description => ($_->{description} // '') } }
                            grep { ref $_ eq 'HASH' } @{ $m->{params} } ];
        }
        \%mo;
    } @{ $t->{members} } ];

    if (ref $t->{examples} eq 'ARRAY' && @{ $t->{examples} }) {
        $o{examples} = [ map { {
            title => ($_->{title} // 'Example'),
            code => ($_->{code} // ''),
            complete => (($_->{complete} && "$_->{complete}" ne '0') ? JSON::PP::true : JSON::PP::false),
            ($_->{description} && $_->{description} =~ /\S/ ? (description => $_->{description}) : ()),
        } } grep { ref $_ eq 'HASH' && ($_->{code} // '') =~ /\S/ } @{ $t->{examples} } ];
        delete $o{examples} unless @{ $o{examples} };
    }
    push @out, \%o;
}

my $data = {
    headerCount => 58,
    sourceBase  => '',
    modules     => \@MODULES,
    guides      => \@GUIDES,
    types       => \@out,
};

my $json = JSON::PP->new->canonical(1)->pretty(0)->utf8(1)->encode($data);

open(my $o, '>:raw', $OUT) or die "cannot write $OUT: $!";
print $o "/* GENERATED - do not edit.\n";
print $o "   Built by tools/apidoc from the headers under engine/include/engine/.\n";
print $o "   Every signature here is the declaration as written in its header. */\n";
print $o "window.ENGINE_API = $json;\n";
close $o;

my $members = 0; my $examples = 0; my %aud;
for my $t (@out) {
    $members += scalar @{ $t->{members} };
    $examples += ref $t->{examples} eq 'ARRAY' ? scalar @{ $t->{examples} } : 0;
    $aud{ $t->{audience} }++;
}
printf "\nwrote %s  (%.0f KB)\n", $OUT, (-s $OUT) / 1024;
printf "  %d types  %d members  %d examples\n", scalar @out, $members, $examples;
printf "  audience: script %d, system %d, host %d\n", $aud{script} // 0, $aud{system} // 0, $aud{host} // 0;
if (%DEFECT_COUNT) {
    print "  verifier defects applied:\n";
    printf "    %-22s %d\n", $_, $DEFECT_COUNT{$_} for sort keys %DEFECT_COUNT;
}
