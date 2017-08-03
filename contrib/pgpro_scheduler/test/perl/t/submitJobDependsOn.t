#!/usr/bin/perl
use strict;
no warnings;
use Test::More;
use DBI;
use Getopt::Long;

my $dbh = require 't/_connect.pl';
ok($dbh->err == 0) or (print $DBI::errstr and BAIL_OUT);

my $query = "DELETE FROM test_results;";
$dbh->do($query);
ok($dbh->err == 0) or (print $DBI::errstr . "\n" and $dbh->disconnect() and BAIL_OUT);

$query = "SELECT schedule.submit_job(\'SELECT pg_sleep(60)\');";
my $sth = $dbh->prepare($query);
ok($sth->execute()) or (print $DBI::errstr . "\n" and $dbh->disconnect() and BAIL_OUT);
my $job_id = $sth->fetchrow_array() and $sth->finish();

$query = "SELECT schedule.submit_job(\'INSERT INTO test_results
            (time_mark, commentary) VALUES(now(), ''submitJobDependsOn'')\', depends_on := \'{$job_id}\');";
my $sth = $dbh->prepare($query);
ok($sth->execute()) or (print $DBI::errstr . "\n" and $dbh->disconnect() and BAIL_OUT);

sleep 10;
$query = "SELECT count(*) FROM test_results";
$sth = $dbh->prepare($query);
ok($sth->execute()) or (print $DBI::errstr . "\n" and $dbh->disconnect() and BAIL_OUT);

my $result = $sth->fetchrow_array() and $sth->finish();
ok ($result == 0) or print "Count != 0\n";

sleep 60;
$query = "SELECT count(*) FROM test_results";
$sth = $dbh->prepare($query);
ok($sth->execute()) or (print $DBI::errstr . "\n" and $dbh->disconnect() and BAIL_OUT);

my $result = $sth->fetchrow_array() and $sth->finish();
ok ($result > 0) or print "Count == 0\n";

$query = "DELETE FROM test_results;";
$dbh->do($query);
ok($dbh->err == 0) or print $DBI::errstr . "\n";

$dbh->disconnect();

done_testing();

