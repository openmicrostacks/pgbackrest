####################################################################################################################################
# EXCEPTION MODULE
####################################################################################################################################
package BackRest::Exception;

use threads;
use strict;
use warnings;
use Carp;

####################################################################################################################################
# Exports
####################################################################################################################################
use Exporter qw(import);
our @EXPORT = qw(ERROR_RESTORE_PATH_NOT_EMPTY);

####################################################################################################################################
# Exception Codes
####################################################################################################################################
use constant
{
    ERROR_RESTORE_PATH_NOT_EMPTY       => 100
};

####################################################################################################################################
# CONSTRUCTOR
####################################################################################################################################
sub new
{
    my $class = shift;       # Class name
    my $iCode = shift;       # Error code
    my $strMessage = shift;  # ErrorMessage

    # Create the class hash
    my $self = {};
    bless $self, $class;

    # Initialize exception
    $self->{iCode} = $iCode;
    $self->{strMessage} = $strMessage;

    return $self;
}

####################################################################################################################################
# CODE
####################################################################################################################################
sub code
{
    my $self = shift;

    return $self->{iCode};
}

####################################################################################################################################
# MESSAGE
####################################################################################################################################
sub message
{
    my $self = shift;

    return $self->{strMessage};
}

1;
