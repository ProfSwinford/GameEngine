#!/usr/bin/perl
# ============================================================================
#  Write every code example in docs/api/api-data.js out as its own .cpp, so
#  the real compiler can be pointed at them.
#
#  Two kinds come out:
#    WHOLE      an example marked complete - a real script file, include,
#               class, registration macro and all. Held to zero errors.
#    FRAGMENT   a member-level snippet, wrapped in a script class so it
#               compiles at all. Most of these refer to a receiver the
#               snippet never declared, which is what a fragment is; see
#               analyse.pl for how that is told apart from a real defect.
#
#    perl tools/apidoc/extract.pl [outDir]        default: build/apidoc-examples
# ============================================================================
use strict;
use warnings;
use JSON::PP;
use File::Basename qw(dirname);
use File::Path qw(make_path);

my $ROOT = dirname(dirname(dirname(__FILE__)));
$ROOT = '.' unless -d "$ROOT/engine/include";

my $OUT = $ARGV[0] // "$ROOT/build/apidoc-examples";
make_path($OUT) unless -d $OUT;
unlink glob "$OUT/*.cpp";

open(my $f, '<:raw', "$ROOT/docs/api/api-data.js") or die "cannot read the reference data: $!\n";
local $/;
my $t = <$f>;
close $f;
$t =~ s/^.*?window\.ENGINE_API\s*=\s*//s;
$t =~ s/;\s*$//s;
my $d = JSON::PP->new->utf8(1)->decode($t);

my $n = 0;
my ($whole, $wrapped) = (0, 0);
my @index;

sub safe {
    my $s = shift;
    $s =~ s/[^A-Za-z0-9]+/_/g;
    $s =~ s/^_+|_+$//g;
    $s = 'x' unless length $s;
    return substr($s, 0, 40);
}

sub emit {
    my ($tag, $code, $kind) = @_;
    $n++;
    my $file = sprintf("%s/ex%03d_%s.cpp", $OUT, $n, safe($tag));
    open(my $o, '>:raw', $file) or die $!;
    print $o $code;
    close $o;
    push @index, sprintf("ex%03d_%s.cpp\t%s\t%s", $n, safe($tag), $tag, $kind // 'frag');
}

# A fragment that is already one or more whole member functions goes straight
# into the class body; anything else is a statement list and needs a function
# around it.
# Not every example is C++. Some show what a thing looks like once it is
# SERIALISED - a fragment of a .oscene file - and feeding those to a compiler
# produces a syntax error that means nothing.
sub looks_like_json {
    my $s = shift // '';
    return 0 if $s =~ /;/;
    return 1 if $s =~ /"\s*:/;
    return 0;
}

my $skipped = 0;

sub wrap {
    my ($tag, $code) = @_;
    if (looks_like_json($code)) { $skipped++; return }
    $wrapped++;
    my $cls = 'Frag' . sprintf('%03d', $n + 1);
    my $body = $code;
    $body =~ s/^/    /mg;
    my $inner = ($body =~ /^\s*(?:void|float|int|bool|auto|eng::|const\s+char\s*\*|const)\s+\**\s*\w+\s*\([^;]*\)\s*(?:const\s*)?(?:override\s*)?\{/m)
        ? $body
        : "    void Run() {\n$body\n    }\n";
    emit($tag, <<"CPP", 'frag');
#include <engine/Engine.h>
using namespace eng;
namespace {
class $cls final : public MyScriptBehaviour {
public:
$inner
};
}   // namespace
CPP
}

for my $ty (@{ $d->{types} }) {
    for my $ex (@{ $ty->{examples} || [] }) {
        my $code = $ex->{code} // '';
        next unless $code =~ /\S/;
        if ($ex->{complete} && $code =~ /ENGINE_REGISTER_SCRIPT/) {
            $whole++;
            emit("$ty->{name}-$ex->{title}", $code . "\n", 'whole');
        } else {
            wrap("$ty->{name}-$ex->{title}", $code);
        }
    }
    for my $m (@{ $ty->{members} }) {
        my $code = $m->{example} // '';
        next unless $code =~ /\S/;
        wrap("$ty->{name}-$m->{name}", $code);
    }
}

open(my $ix, '>:raw', "$OUT/index.tsv") or die $!;
print $ix "$_\n" for @index;
close $ix;

printf "wrote %d translation units to %s\n", $n, $OUT;
printf "  %d whole script files, %d fragments wrapped in one\n", $whole, $wrapped;
printf "  %d skipped as not C++ (serialised JSON shown in a page)\n", $skipped if $skipped;
