%-- 30/03/2015 15:59:10 --%

close all;
%clear all;
figure(1)

%%   Read the numeric data in the file.


%fid = fopen('aloha_slotted_tdm_icmp_1s.csv');
%fid = fopen('tdm_1_tdm_5_icmp_1s.csv');
%fid = fopen('aloha_slotted_tdm_udp_saturation.csv');
fid = fopen('tdm_1_tdm_5_udp_saturation.csv');

HDRS = textscan(fid,'%s %s %s %s %s %s %s %s %s %s %s %s %s',1, 'delimiter',',');
%num-row,num-read,um-from-start-real,um-from-start-compute,um-diff-time,bytecode-protocol,count-slot,count-slot-var,packet_to_transmit,my_transmission,succes_transmission,other_transmission
data = textscan(fid,'%d %d %d %d %d %d %d %d %d %d %d %d','delimiter',',');
fclose(fid);


%% columns extract
time_experiment = data{:,4};
data_experiment_1 = data{:,9};
data_experiment_2 = data{:,10};
data_experiment_3 = data{:,11};
data_experiment_4 = data{:,12};

%% plot metaMAC parameter
bar(time_experiment, [data_experiment_1 data_experiment_2, data_experiment_3, data_experiment_4], 0.5, 'stack');
legend('packet\_to\_transmit', 'my\_transmission', 'success\_transmission', 'other\_transmissione');

%% plot protocol active
hold on;
plot(time_experiment, data{:,6}+1);

