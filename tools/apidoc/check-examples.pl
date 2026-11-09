#!/usr/bin/perl
# Cross-check every code example in api-data.js against the harvested API.
#
# Three checks, strongest first:
#   1. Type::Member(...)  - the type must exist and must have that member
#   2. eng::Type          - the type must exist
#   3. ->Member( / .Member( - the name must be a member of SOMETHING, or a
#                            known std or local name. Lower signal, reported
#                            separately.
#   4. recv.Member(         - when the snippet DECLARES recv's type and that
#                            type is in the reference, the member must be on
#                            THAT type. Check 3 cannot see this: it asks only
#                            whether the name belongs to anything, and
#                            `log.LogWarningOnce(...)` passed for months
#                            because `LogWarningOnce` is a member of `Debug` -
#                            just not of `Debug::Channel`, which is what `log`
#                            actually was. That example did not compile.
use strict;
use warnings;
use JSON::PP;

use File::Basename qw(dirname);
my $ROOT = dirname(dirname(dirname(__FILE__)));
$ROOT = "." unless -d "$ROOT/engine/include";
my $P = "$ROOT/docs/api/api-data.js";
open(my $f, '<:raw', $P) or die $!;
local $/;
my $t = <$f>;
close $f;
$t =~ s/^.*?window\.ENGINE_API\s*=\s*//s;
$t =~ s/;\s*$//s;
my $d = JSON::PP->new->utf8(1)->decode($t);

my (%TYPE, %MEMBERS, %ANYMEMBER, %ALIAS);

# `typedef eng::InputMap Input;` and the rest of Engine.h's Unity spellings are
# TYPES as far as an example is concerned, even though the reference lists them
# as members of an alias group. Resolve them before checking anything.
for my $ty (@{ $d->{types} }) {
    for my $m (@{ $ty->{members} }) {
        next unless ($m->{kind} // '') eq 'alias';
        my $s = $m->{signature} // '';
        if ($s =~ /typedef\s+(?:eng::)?(\w+)\s*\*?\s*(\w+)\s*;/) { $ALIAS{$2} = $1 }
        elsif ($s =~ /using\s+(\w+)\s*=\s*(?:eng::)?(\w+)/)      { $ALIAS{$1} = $2 }
    }
}

my %BASE;

for my $ty (@{ $d->{types} }) {
    $TYPE{ $ty->{name} } = 1;

    # A MEMBER CAN BE INHERITED. `collider->WorldBounds()` is declared on
    # `ColliderComponent`, not on the `BoxColliderComponent` the snippet
    # writes - so check 4 below has to walk the chain or it reports six
    # perfectly good examples as broken.
    if (defined $ty->{inherits}) {
        $BASE{ $ty->{name} } =
            [ ref $ty->{inherits} eq 'ARRAY' ? @{ $ty->{inherits} } : ($ty->{inherits}) ];
    }

    # A NESTED TYPE IS ALSO A MEMBER OF ITS ENCLOSING ONE. The reference lists
    # `Debug::Channel` and `ScriptRegistry::Entry` as types with pages of their
    # own, and an example that writes `Debug::Channel log = ...` was reported
    # as "`Debug` has no member `Channel`" - which is a blind spot here rather
    # than a missing symbol, because the data says the type exists two lines
    # above.
    if ($ty->{name} =~ /^(.+)::(\w+)$/) {
        $MEMBERS{$1}{$2} = 1;
        $ANYMEMBER{$2}   = 1;
    }

    for my $m (@{ $ty->{members} }) {
        my $n = $m->{name};
        $n =~ s/\s*\(.*\)\s*$//;
        $MEMBERS{ $ty->{name} }{$n} = 1;
        $ANYMEMBER{$n} = 1;
        # a property pair is reachable under all its real spellings too
        for my $part (split /\s*\|\s*/, ($m->{signature} // '')) {
            if ($part =~ /([~A-Za-z_]\w*)\s*\(/) {
                $MEMBERS{ $ty->{name} }{$1} = 1;
                $ANYMEMBER{$1} = 1;
            }
        }
    }
}

# names that are fine without being in the API
my %OK = map { $_ => 1 } qw(
    Json nlohmann Input
    std eng hooks fields Channels MessageTypes SystemStage Neighbour
    MyScriptBehaviour ENGINE_REGISTER_SCRIPT
    ENGINE_LOG ENGINE_LOG_INFO ENGINE_LOG_WARN ENGINE_LOG_ERROR
    string string_view vector optional array to_string size max min abs clamp
    fabs sqrt sin cos atan2 floor ceil round fmod printf snprintf format
    make_unique move forward swap sort find_if begin end data size_t numeric_limits
    is_base_of_v nullptr_t initializer_list
    size empty clear push_back emplace_back reserve resize at front back
    count erase insert contains value_or has_value reset get
);

# Does $ty, or anything it inherits from, have $mem? Returns 'yes', 'no', or
# 'unknown' when the chain leaves the reference - an undocumented base could
# be where the member lives, and guessing would make this gate lie.
sub member_on_chain {
    my ($ty, $mem) = @_;
    my @queue = ($ty);
    my %seen;
    my $complete = 1;
    while (@queue) {
        my $cur = shift @queue;
        next if $seen{$cur}++;
        return 'yes' if $MEMBERS{$cur}{$mem};
        for my $b (@{ $BASE{$cur} || [] }) {
            if ($TYPE{$b}) { push @queue, $b }
            else           { $complete = 0 }
        }
    }
    return $complete ? 'no' : 'unknown';
}

my (@hard, @soft);
my $examples = 0;
my $lines = 0;

sub check {
    my ($where, $code) = @_;
    return unless defined $code && $code =~ /\S/;
    $examples++;
    $lines += scalar(() = $code =~ /\n/g) + 1;

    my $src = $code;
    $src =~ s{//[^\n]*}{}g;
    $src =~ s{/\*.*?\*/}{ }gs;
    $src =~ s{"(?:\\.|[^"\\])*"}{""}g;

    # 1. Qualified calls and qualified names
    while ($src =~ /\b([A-Z]\w*)::(\w+)/g) {
        my ($ty, $mem) = ($1, $2);
        next if $OK{$ty};
        $ty = $ALIAS{$ty} while exists $ALIAS{$ty} && $ALIAS{$ty} ne $ty;
        next if $OK{$ty};
        if (!$TYPE{$ty}) {
            push @hard, "$where: no type `${ty}` (in `${ty}::$mem`)";
            next;
        }
        next if $MEMBERS{$ty}{$mem};
        push @hard, "$where: `${ty}` has no member `$mem`";
    }

    # 2. eng::Thing
    while ($src =~ /\beng::(\w+)/g) {
        my $n = $1;
        next if $OK{$n} || $TYPE{$n} || $ANYMEMBER{$n};
        push @hard, "$where: no `eng::$n` in the reference";
    }

    # 3. unqualified member calls
    while ($src =~ /(?:->|\.)(\w+)\s*\(/g) {
        my $n = $1;
        next if $OK{$n} || $ANYMEMBER{$n};
        push @soft, "$where: `->$n(...)` is not a member of anything documented";
    }

    # 4. a receiver whose type this snippet declares. Additive: check 3 above
    #    still runs over every call, so nothing loses coverage - this only
    #    upgrades the ones we can actually resolve.
    my %local;
    while ($src =~ /\b(?:const\s+|static\s+|constexpr\s+|inline\s+)*
                    (?:eng::)?([A-Z]\w*(?:::\w+)*)
                    \s*[*&]?\s*(\w+)\s*(?:=|;|\{)/gx) {
        my ($ty, $var) = ($1, $2);
        $ty =~ s/^eng:://;
        $ty = $ALIAS{$ty} while exists $ALIAS{$ty} && $ALIAS{$ty} ne $ty;
        next unless $TYPE{$ty};
        $local{$var} = $ty;
    }
    while ($src =~ /\b(\w+)\s*(?:->|\.)(\w+)\s*\(/g) {
        my ($recv, $n) = ($1, $2);
        my $ty = $local{$recv} or next;
        my $v = member_on_chain($ty, $n);
        next if $v eq 'yes';
        if ($v eq 'unknown') {
            push @soft, "$where: `->$n(...)` on `$recv`: `$ty` inherits from outside the reference";
            next;
        }
        push @hard, "$where: `$ty` has no member `$n` (called on `$recv`, which this snippet declares `$ty`)";
    }
}

for my $ty (@{ $d->{types} }) {
    for my $ex (@{ $ty->{examples} || [] }) {
        check("$ty->{name} example \"$ex->{title}\"", $ex->{code});
    }
    for my $m (@{ $ty->{members} }) {
        check("$ty->{name}.$m->{name} example", $m->{example}) if $m->{example};
    }
}

printf "checked %d examples, %d lines of C++\n\n", $examples, $lines;

my %seen;
printf "HARD failures (a name that is definitely not there): %d\n", scalar @hard;
for my $h (@hard) { print "  $h\n" unless $seen{$h}++ }
printf "\nSOFT warnings (unresolvable receiver, may be a local or std name): %d\n", scalar @soft;
my %sc;
$sc{$_}++ for map { /`->(\w+)\(/ ? $1 : $_ } @soft;
for my $k (sort { $sc{$b} <=> $sc{$a} || $a cmp $b } keys %sc) {
    printf "  %-28s x%d\n", $k, $sc{$k};
}
