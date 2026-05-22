#!/usr/bin/perl -w 
#
#
# Script preparsuje opcodes_dasm.c a vyrobi z nej seznam opkodu pro inline assembler
#
#




use strict;

open ( IN_FILE, "../../emulator/cpu/z80ex-mz800/opcodes/opcodes_dasm.c" );


sub char2hex ( $ ) {
    my $c = shift;
    
    my $n = ord ( $c );
    
    if ( ( $n >= ord ( '0' ) ) && ( $n <= ord ( '9' ) ) ) {
        return $n - ord ( '0' );
    };
    return $n - ord ( 'A' ) + 0x0a;
}



my %all_ins = ();
my %ASM = ();

my $prefix = "";

while ( <IN_FILE> ) {
    if ( ! /\{/ ) {
        goto NEXT_ROW;
    };
    if ( /NULL/ ) {
        goto NEXT_ROW;
    };
    
    if ( ! /^{/ ) {
        chomp;
        s/^static const z80ex_opc_dasm dasm_//;
        s/\[.*$//;
        if ( /^base$/ ) {
            $_ = "";
        };
        tr /[a-z]/[A-Z]/;
        #print $_ ."\n";
        $prefix = $_;
        goto NEXT_ROW;
    };
    
    m/{ "(.*)".*\/\* (..).*$/;
    
    my $full_instruction = $1;
    my $main_opt = $2;
    
    if ( ( /ignore/ ) || ( /shift/ ) ) {
        goto NEXT_ROW;
    };

    my $command;
    my $params;
    
    $_ = $full_instruction;
    
    if ( m/^(\w+) (.*)$/ ) {
        $command = $1;
        $params = $2;
    } else {
        $command = $_;
        $params = "";
    };

    m/^(.).*/;
    my $first_char = $1;




    # vyradime duplicitni instrukce (18 dup)
    if ( $full_instruction eq "NEG" ) {
        if ( $main_opt ne "44" ) {
            goto NEXT_ROW;
        };
    };
    if ( $full_instruction eq "IM 0" ) {
        if ( $main_opt ne "46" ) {
            goto NEXT_ROW;
        };
    };
    if ( $full_instruction eq "IM 1" ) {
        if ( $main_opt ne "56" ) {
            goto NEXT_ROW;
        };
    };
    if ( $full_instruction eq "IM 2" ) {
        if ( $main_opt ne "5E" ) {
            goto NEXT_ROW;
        };
    };
    if ( $full_instruction eq "RETI" ) {
        if ( $main_opt ne "4D" ) {
            goto NEXT_ROW;
        };
    };
    if ( $full_instruction eq "RETN" ) {
        if ( $main_opt ne "45" ) {
            goto NEXT_ROW;
        };
    };
    if ( $full_instruction eq "LD (\@),HL" ) {
        if ( $main_opt ne "22" ) {
            goto NEXT_ROW;
        };
    };
    if ( $full_instruction eq "LD HL,(\@)" ) {
        #print "REMOVE: " . $full_instruction . " - code: " . $main_opt . "\n";
        if ( $main_opt ne "2A" ) {
            goto NEXT_ROW;
        };
    };
    if ( ( $command eq "BIT" ) && ( $params =~ /I/ ) ) {
        $main_opt =~ m/(.)(.)/; # :)
        my $value = char2hex ( $1 ) << 4;
        $value |= char2hex ( $2 );
        #printf ( "0x%02x\n", $value );
        if ( ( $value & 0xc7 ) != 0x45 ) {
            goto NEXT_ROW;
        };
    };
    if ( exists ( $all_ins { $full_instruction } ) ) {
        print "DUP: " . $full_instruction . "\n";
        goto NEXT_ROW;
    };


    if ( /@/ ) {
        $all_ins { $full_instruction } = $prefix . $main_opt . "\@";
    } elsif ( /%/ ) {
        $all_ins { $full_instruction } = $prefix . $main_opt . "%";
    } elsif ( /\$/ ) {
        if ( /#/ ) {
            $all_ins { $full_instruction } = $prefix . $main_opt . "\$#";
        } else {
            $all_ins { $full_instruction } = $prefix . "\$" . $main_opt;
        };
    } elsif ( /#/ ) {
        $all_ins { $full_instruction } = $prefix . $main_opt . "#";
    } else {
        $all_ins { $full_instruction } = $prefix . $main_opt;
    };





    if ( ! exists ( $ASM { $first_char } ) ) {
        $ASM { $first_char } = ();
    };
        
    if ( ! exists ( ${ $ASM { $first_char } } { $command } ) ) {
        ${ $ASM { $first_char } } { $command } = ();
    };

    ${ ${ $ASM { $first_char } } { $command } } { $params } = $all_ins { $full_instruction };
    

    #print $first_char."\n";
    #print $command."\n";
    #print $params."\n";
    #print $full_instruction."\n";
    #print $main_opt."\n";
    
    NEXT_ROW:
};

close ( IN_FILE );




#
#
#

my $all_chars = "";

foreach my $char ( sort ( keys ( %ASM ) ) ) {

#if ( $char ne "A" ) {
#    goto SKIP_CHAR;
#};

    $all_chars .= "\t{ '" . $char ."', iasm_commands_" . $char . " },\n";

    my $all_commands = "";
        
    foreach my $cmd ( sort ( keys ( $ASM { $char } ) ) ) {

        $all_commands .= "\t{ \"" . $cmd . "\", iasm_opts_" . $cmd ." },\n";

        print ( "static const iasm_opts_t iasm_opts_" . $cmd ." [] = {\n" );

        # params jen bez variables
        foreach my $cmd_params ( sort ( keys ( ${ $ASM { $char } } { $cmd } ) ) ) {
            $_ = $cmd_params;
            if ( ! /[\$|@|#|%]/ ) {
                my $str_cmd_params;
                if ( $cmd_params ne "" ) {
                    $str_cmd_params = "\"" . $cmd_params . "\"";
                } else {
                    $str_cmd_params = "NULL";
                };
                print ( "\t{ 0, "  . length ( $cmd_params ) . ", 0, " . $str_cmd_params . ", \"" . ${ ${ $ASM { $char } } { $cmd } } { $cmd_params } ."\" },\n");
            };
        };

        # params jen s variables
        foreach my $cmd_params ( sort ( keys ( ${ $ASM { $char } } { $cmd } ) ) ) {
            $_ = $cmd_params;
            if ( /[\$|@|#|%]/ ) {
                my $count_variables = 1;
                if ( ( /\$/ ) && ( /#/ ) ) {
                    $count_variables++;
                };
                my $str_cmd_params;
                if ( $cmd_params ne "" ) {
                    $str_cmd_params = "\"" . $cmd_params . "\"";
                } else {
                    $str_cmd_params = "NULL";
                };
                my ( $par1, $par2, $par3 ) = split ( /[\$|@|#|%]/, $cmd_params );
                print ( "\t{ " . $count_variables . ", " . length ( $par1 ) . ", " . length ( $par2 ) . ", " . $str_cmd_params . ", \"" . ${ ${ $ASM { $char } } { $cmd } } { $cmd_params } ."\" },\n");
            };
        };
        print "\t{ -1, -1, -1, NULL, NULL }\n};\n\n";
    };
    
    print ( "static const iasm_commands_t iasm_commands_" . $char . " [] = {\n" );
    print $all_commands . "\t{ NULL, NULL }\n};\n\n";
    
SKIP_CHAR:
    
};


print ( "static const iasm_chars_t iasm_chars [] = {\n" );
print $all_chars . "\t{ 0x00, NULL }\n};\n";
