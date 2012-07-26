function [time,power]=energino(log, polling)

    fid=fopen(log);
    data=textscan(fid, '%f-%f-%f %f:%f:%f,%f %f[V] %f[A] %f[W] %f[samples] %f[window]');
    fclose(fid);

    power=data{10};

    time=0:polling:polling*(length(power)-1);
    time=time/1000;

    figure1 = figure;
    axes('Parent',figure1,'FontSize',14,'FontName','Arial');
    hold on;
    box on;
    grid on;
    axis([ 0 max(time) min(power) * 0.95 max(power) * 1.05])
    plot(time,power,'MarkerFaceColor','k','MarkerSize',4,'Marker','none','LineStyle','-','Color','b')
    xlabel('Time (s)');
    ylabel('Power (W)');

    for i=1:1000

        fid = fopen('energino.log');
        data= textscan(fid, '%f-%f-%f %f:%f:%f,%f %f[V] %f[A] %f[W] %f[samples] %f[window]');
        fclose(fid);

        power=data{10};

        time = 0:polling:polling*(length(power)-1);
        time=time/1000;

        axis([ 0 max(time) min(power) * 0.95 max(power) * 1.05])
        plot(time,power,'MarkerFaceColor','k','MarkerSize',4,'Marker','none','LineStyle','-','Color','b')

        pause(polling/1000)

    end

end
